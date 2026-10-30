// Aliro reader task + wake window. See homekey.h.
//
// Derived from ultrawidelock/apps/esp32-matter-lock/main/app_main.cpp
// (ultrawidelock_reader_task), with the always-on approach/relock loop replaced
// by a bounded wake window and a momentary, stateless grant.

#include "homekey.h"

#include <atomic>

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_gap.h" // ble_gap_adv_active()
#include "host/ble_hs.h"  // ble_hs_synced()

#include <ultrawidelock/reader.h>
#include <ultrawidelock/uwb.h>
#include <ultrawidelock_approach.h>
#include <ultrawidelock_ble.h> // ultrawidelock_ble_set_adv_enabled()
#include <ultrawidelock_lat.h>

#include "app_priv.h"

static const char *TAG = "homekey";

/* Approach controller tuning (upstream's bench values; see their comments in
 * ultrawidelock/apps/esp32-matter-lock/main/app_main.cpp). near_dwell and the
 * ETA prediction are Kconfig (latency vs. robustness, see Kconfig.projbuild). */
#define HOMEKEY_RELOCK_RANGE_CM 400
#define HOMEKEY_FAR_DWELL       3

/* After the momentary relock, keep the link up this long so the Wallet
 * "Secured" status reaches the phone before the gate drops the connection. */
#define HOMEKEY_CLOSE_LINGER_MS 500

static TaskHandle_t s_task;

/* Requests from other tasks, consumed by the reader task. */
static std::atomic<bool> s_req_wake{false};
static std::atomic<bool> s_req_sleep{false};

/* Window state, owned by the reader task; mirrored for readers on other tasks. */
static std::atomic<bool> s_open{false};
static std::atomic<int64_t> s_deadline_ms{0};

/* Latency trace: when the wake request arrived (any task), stamped once per window. */
static std::atomic<int64_t> s_wake_req_ms{0};

static int64_t now_ms(void)
{
	return esp_timer_get_time() / 1000;
}

static void kick(void)
{
	if (s_task != nullptr) {
		xTaskNotifyGive(s_task);
	}
}

void homekey_init(void)
{
	/* Before start_attached(): the reader then comes up without ever emitting
	 * the credential advertisement. */
	ultrawidelock_ble_set_adv_enabled(false);
}

void homekey_wake(const char *why)
{
	ESP_LOGW(TAG, "wake requested (%s)", why ? why : "?");
	if (!s_open) {
		s_wake_req_ms = now_ms();
	}
	s_req_sleep = false;
	s_req_wake = true;
	kick();
}

void homekey_sleep(const char *why)
{
	if (!s_open && !s_req_wake) {
		return; /* already closed: idempotent (the attribute echo lands here) */
	}
	ESP_LOGW(TAG, "sleep requested (%s)", why ? why : "?");
	s_req_wake = false;
	s_req_sleep = true;
	kick();
}

bool homekey_window_open(void)
{
	return s_open;
}

int32_t homekey_window_remaining_ms(void)
{
	if (!s_open) {
		return 0;
	}
	int64_t left = s_deadline_ms - now_ms();
	return left > 0 ? (int32_t)left : 0;
}

// Range-latch listener: runs on the UWB RX path, so it only stamps the latency
// trace and wakes the reader task; the decision stays on the task.
static void on_uwb_range(void)
{
	ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_FIRST_RANGE);

	int32_t cm;
	if (ultrawidelock_uwb_trusted_range_cm(&cm)) {
		ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_TRUSTED_RANGE);
	}
	if (s_task == nullptr) {
		return;
	}
	if (xPortInIsrContext()) {
		BaseType_t woken = pdFALSE;
		vTaskNotifyGiveFromISR(s_task, &woken);
		portYIELD_FROM_ISR(woken);
	} else {
		xTaskNotifyGive(s_task);
	}
}

// A key learned from a device's Access Document (an Apple Watch on the owner's
// Apple ID) rather than a Matter SetCredential. BLE-host task: log only.
static void on_credential_learned(uint8_t cred_type, uint16_t cred_index, uint16_t user_index,
				  const uint8_t cred_pub[65])
{
	ESP_LOGI(TAG, "credential learned from Access Document: type %u idx %u user %u, key %02x%02x%02x%02x...",
		 (unsigned)cred_type, (unsigned)cred_index, (unsigned)user_index, cred_pub[0],
		 cred_pub[1], cred_pub[2], cred_pub[3]);
}

static void approach_reset(struct ultrawidelock_approach *ap)
{
	struct ultrawidelock_approach_cfg acfg;

	ultrawidelock_approach_defaults(&acfg);
	acfg.unlock_cm = CONFIG_HOMEKEY_UNLOCK_CM;
	acfg.relock_cm = HOMEKEY_RELOCK_RANGE_CM;
	acfg.near_dwell = CONFIG_HOMEKEY_NEAR_DWELL;
	acfg.far_dwell = HOMEKEY_FAR_DWELL;
#if defined(CONFIG_HOMEKEY_APPROACH_PREDICT)
	/* Grant when the predicted arrival at unlock_cm is within motor+margin:
	 * a walking approach is granted ~that long before reaching the radius. */
	acfg.predict_en = true;
	acfg.motor_ms = CONFIG_HOMEKEY_PREDICT_LEAD_MS;
	acfg.margin_ms = 0;
#else
	acfg.predict_en = false;
#endif
	ultrawidelock_approach_init(ap, &acfg);
}

static void homekey_task(void *arg)
{
	(void)arg;

	/* The reader can only take the shared legacy advertiser once the host is
	 * synced and Matter has released it. Capped so a stuck advertiser still
	 * lets the start run and log its failure. */
	for (int waited_ms = 0; waited_ms < 5000; waited_ms += 50) {
		if (ble_hs_synced() && ble_gap_adv_active() == 0) {
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}

	ultrawidelock_uwb_set_range_listener(on_uwb_range);
	ultrawidelock_reader_set_credential_learned_listener(on_credential_learned);

	int rc = ultrawidelock_reader_start_attached();
	ESP_LOGW(TAG, "reader start_attached() = %d (%s)", rc,
		 rc == 0 ? "attached, advertising gated off until wake" : "FAILED");

	struct ultrawidelock_approach approach;
	approach_reset(&approach);

	bool session_was_up = false;
	uint32_t last_start_gen = ultrawidelock_uwb_start_generation();

	bool granted = false;       /* this window already granted once */
	int64_t open_ms = 0;
	/* Range-latch generation last consumed. Only a range latched AFTER this
	 * counter moved is fed to the approach controller: the latch keeps the
	 * previous session's distance, and a task wake (e.g. the wake request
	 * itself) must not replay it as a fresh approach. */
	uint32_t last_range_gen = ultrawidelock_uwb_range_generation();
	bool wallet_unsecured = false; /* Wallet told "unlocked", not yet "secured" */
	int64_t grant_ms = 0;

	for (;;) {
		uint32_t woke = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
		int64_t now = now_ms();

		ultrawidelock_reader_status_tick(now);

		/* ---- window open / close requests ---------------------------- */
		if (s_req_wake.exchange(false)) {
			s_deadline_ms = now + (int64_t)CONFIG_HOMEKEY_WAKE_WINDOW_S * 1000;
			if (!s_open) {
				last_range_gen = ultrawidelock_uwb_range_generation();
				approach_reset(&approach);
				session_was_up = false;
				last_start_gen = ultrawidelock_uwb_start_generation();
				granted = false;
				wallet_unsecured = false;
				open_ms = now;
				s_open = true;
				ultrawidelock_ble_set_adv_enabled(true);
				board_led(true);
				app_report_wake(true);
				ESP_LOGW(TAG, "window OPEN for %d s", CONFIG_HOMEKEY_WAKE_WINDOW_S);
			} else {
				ESP_LOGW(TAG, "window extended to %d s", CONFIG_HOMEKEY_WAKE_WINDOW_S);
			}
		}

		bool close_now = s_req_sleep.exchange(false);

		if (!s_open) {
			continue;
		}

		/* ---- approach controller (window open) ------------------------ */
		const bool session_now = ultrawidelock_reader_session_active();
		if (session_now && !session_was_up) {
			/* A session only comes up with the phone approaching (the RSSI
			 * gate holds it off until the link is strong): arm the
			 * trajectory gate on that edge, as upstream does. */
			ultrawidelock_approach_session_up(&approach);
		}
		session_was_up = session_now;
		{
			uint32_t start_gen = ultrawidelock_uwb_start_generation();
			if (start_gen != last_start_gen) {
				last_start_gen = start_gen;
				ultrawidelock_approach_session_up(&approach);
			}
		}

		/* Fresh = at least one range latched since we last looked. */
		const uint32_t range_gen = ultrawidelock_uwb_range_generation();
		const bool fresh = (range_gen != last_range_gen);
		last_range_gen = range_gen;
		(void)woke;

		int32_t cm = 0;
		bool trusted = fresh && ultrawidelock_uwb_trusted_range_cm(&cm);
		enum ultrawidelock_approach_action act =
			trusted ? ultrawidelock_approach_feed(&approach, now, cm)
				: ultrawidelock_approach_tick(&approach, now);

		if (trusted) {
			ESP_LOGW(TAG, "range %d cm (est %d cm) +%d ms", (int)cm,
				 (int)ultrawidelock_approach_est_cm(&approach), (int)(now - open_ms));
		} else if (fresh) {
			int32_t raw;
			if (ultrawidelock_uwb_last_range_cm(&raw)) {
				ESP_LOGW(TAG, "range %d cm UNTRUSTED (building consensus) +%d ms",
					 (int)raw, (int)(now - open_ms));
			}
		}

		if (!granted && (act == ULTRAWIDELOCK_APPROACH_UNLOCK_THRESHOLD ||
				 act == ULTRAWIDELOCK_APPROACH_UNLOCK_PREDICT)) {
			ESP_LOGW(TAG, "GRANT: key at %d cm (est)",
				 (int)ultrawidelock_approach_est_cm(&approach));
			ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_NEAR_DWELL);
			app_aliro_unlock();       /* Door Lock -> Unlocked, momentary */
			app_report_auth_pulse();  /* "homekey auth" sensor */
			/* Reader-Status-Changed -> Unsecured: the Wallet animation gate. */
			ultrawidelock_reader_notify_unlock(true);
			granted = true;
			wallet_unsecured = true;
			grant_ms = now;
			/* Latency budget: device-side wake -> window -> grant, then the
			 * reader's per-phase trace (+ms from the phone's BLE connect). */
			ESP_LOGW(TAG, "homekey-lat: wake->open %d ms, open->grant %d ms, total %d ms",
				 (int)(open_ms - s_wake_req_ms), (int)(now - open_ms),
				 (int)(now - s_wake_req_ms));
			ultrawidelock_lat_report();
			/* Close shortly after the momentary relock. */
			s_deadline_ms = now + CONFIG_HOMEKEY_RELOCK_MS + HOMEKEY_CLOSE_LINGER_MS;
		}

		/* Momentary relock: tell Wallet "secured" while the link is up. */
		if (wallet_unsecured && now - grant_ms >= CONFIG_HOMEKEY_RELOCK_MS) {
			ultrawidelock_reader_notify_unlock(false);
			wallet_unsecured = false;
		}

		if (now >= s_deadline_ms) {
			close_now = true;
		}

		if (close_now) {
			if (wallet_unsecured) {
				ultrawidelock_reader_notify_unlock(false);
				wallet_unsecured = false;
			}
			s_open = false;
			/* Posted after any pending status send on the same host queue, so
			 * the Secured notification goes out before the link is dropped. */
			ultrawidelock_ble_set_adv_enabled(false);
			board_led(false);
			app_report_wake(false);
			ESP_LOGW(TAG, "window CLOSED (%s)", granted ? "granted" : "no grant");
		}
	}
}

void homekey_reader_start_once(void)
{
	static bool started = false;
	if (started) {
		return;
	}
	started = true;
	/* 6 KiB: upstream measured a ~3.2 KiB peak for this task. */
	xTaskCreate(homekey_task, "homekey", 6144, nullptr, 5, &s_task);
	ESP_LOGW(TAG, "reader task started");
}

void homekey_print_status(void)
{
	int32_t cm;

	printf("window    : %s", s_open ? "OPEN" : "closed");
	if (s_open) {
		printf(" (%d ms left)", (int)homekey_window_remaining_ms());
	}
	printf("\nadv gate  : %s\n", ultrawidelock_ble_adv_enabled() ? "open" : "closed");
	printf("session   : %s\n", ultrawidelock_reader_session_active() ? "active" : "none");
	if (ultrawidelock_uwb_last_range_cm(&cm)) {
		printf("last range: %d cm\n", (int)cm);
	} else {
		printf("last range: none\n");
	}
	if (ultrawidelock_uwb_trusted_range_cm(&cm)) {
		printf("trusted   : %d cm\n", (int)cm);
	} else {
		printf("trusted   : none\n");
	}
	if (s_task != nullptr) {
		printf("stack     : %u B free\n",
		       (unsigned)(uxTaskGetStackHighWaterMark(s_task) * sizeof(StackType_t)));
	} else {
		printf("reader    : not started (commission first)\n");
	}
}
