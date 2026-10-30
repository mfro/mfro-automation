// mfro-homekey-uwb application main.
//
// Matter node (Thread) with three endpoints:
//   - Door Lock with Aliro Provisioning + BLE-UWB (Apple Home Key). Momentary:
//     a grant (or remote unlock) reports Unlocked, then Locked again after
//     CONFIG_HOMEKEY_RELOCK_MS. There is no bolt.
//   - On/Off plug-in unit "UWB wake": on opens the reader's wake window, and it
//     reports off again when the window closes.
//   - Occupancy sensor "homekey auth": pulses occupied on each grant.
//
// Derived from ultrawidelock/apps/esp32-matter-lock/main/app_main.cpp.

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <esp_matter.h>

#include <common_macros.h>
#include <app_priv.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#include <button_gpio.h>
#include <iot_button.h>

#include <vector>
#include "host/ble_gatt.h"
#include <platform/ESP32/BLEManagerImpl.h> // BLEMgrImpl().ConfigureExtraServices()
#include <esp_netif_sntp.h>

#include <ultrawidelock/reader.h>
#include <ultrawidelock_ble.h> // ultrawidelock_ble_time_updated()
#include <ultrawidelock_reader_delegate.h>
#include "door_lock_manager.h"
#include "homekey.h"
#include "app_shell.h"

static const char *TAG = "app_main";

uint16_t door_lock_endpoint_id = 0;
uint16_t wake_endpoint_id = 0;
uint16_t auth_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip;

constexpr auto k_timeout_seconds = 300;

/* ======================================================================
 * Board: Seeed XIAO ESP32-C6
 * ====================================================================== */

/* XIAO ESP32-C6 RF switch: GPIO3 low powers it, GPIO14 low selects the onboard
 * ceramic antenna (high = U.FL). Both are reserved; never wire anything to them. */
#define XIAO_RF_SWITCH_EN_GPIO  GPIO_NUM_3
#define XIAO_RF_ANT_SELECT_GPIO GPIO_NUM_14

void board_init()
{
	gpio_config_t rf = {};
	rf.pin_bit_mask = (1ULL << XIAO_RF_SWITCH_EN_GPIO) | (1ULL << XIAO_RF_ANT_SELECT_GPIO);
	rf.mode = GPIO_MODE_OUTPUT;
	gpio_config(&rf);
	gpio_set_level(XIAO_RF_SWITCH_EN_GPIO, 0);
	gpio_set_level(XIAO_RF_ANT_SELECT_GPIO, 0);

#if CONFIG_HOMEKEY_LED_GPIO >= 0
	gpio_config_t led = {};
	led.pin_bit_mask = 1ULL << CONFIG_HOMEKEY_LED_GPIO;
	led.mode = GPIO_MODE_OUTPUT;
	gpio_config(&led);
	gpio_set_level((gpio_num_t)CONFIG_HOMEKEY_LED_GPIO, 1); /* active low: off */
#endif
}

void board_led(bool on)
{
#if CONFIG_HOMEKEY_LED_GPIO >= 0
	gpio_set_level((gpio_num_t)CONFIG_HOMEKEY_LED_GPIO, on ? 0 : 1);
#else
	(void)on;
#endif
}

static void on_button_long_press(void *button_handle, void *usr_data)
{
	(void)button_handle;
	(void)usr_data;
	app_commissioning_window_open();
}

static void on_button_single_click(void *button_handle, void *usr_data)
{
	(void)button_handle;
	(void)usr_data;
	homekey_wake("button");
}

void board_button_init()
{
#if CONFIG_HOMEKEY_BUTTON_GPIO >= 0
	button_config_t cfg = {};
	button_gpio_config_t gcfg = {};
	gcfg.gpio_num = CONFIG_HOMEKEY_BUTTON_GPIO;
	gcfg.active_level = 0;
	button_handle_t btn = nullptr;

	if (iot_button_new_gpio_device(&cfg, &gcfg, &btn) != ESP_OK || btn == nullptr) {
		ESP_LOGW(TAG, "button init failed");
		return;
	}
	iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, on_button_long_press, NULL);
	iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, on_button_single_click, NULL);
#endif
}

/* ======================================================================
 * Endpoint reporting (wake switch, auth sensor, momentary lock)
 * ====================================================================== */

/* Set while we write an attribute ourselves, so app_attribute_update_cb does not
 * treat our own report as a controller command. Matter task only. */
static bool s_internal_update;

static void update_bool_attr(uint16_t ep, uint32_t cluster, uint32_t attr, bool v)
{
	attribute_t *a = attribute::get(ep, cluster, attr);
	if (a == nullptr) {
		return;
	}
	esp_matter_attr_val_t val;
	attribute::get_val(a, &val);
	if (val.val.b == v) {
		return;
	}
	val.val.b = v;
	s_internal_update = true;
	attribute::update(ep, cluster, attr, &val);
	s_internal_update = false;
}

void app_report_wake(bool on)
{
	chip::DeviceLayer::SystemLayer().ScheduleLambda([on]() {
		update_bool_attr(wake_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, on);
	});
}

static void auth_pulse_end(chip::System::Layer *, void *)
{
	attribute_t *a = attribute::get(auth_endpoint_id, OccupancySensing::Id,
					OccupancySensing::Attributes::Occupancy::Id);
	if (a == nullptr) {
		return;
	}
	esp_matter_attr_val_t val;
	attribute::get_val(a, &val);
	val.val.u8 = 0;
	attribute::update(auth_endpoint_id, OccupancySensing::Id,
			  OccupancySensing::Attributes::Occupancy::Id, &val);
}

void app_report_auth_pulse()
{
	chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
		attribute_t *a = attribute::get(auth_endpoint_id, OccupancySensing::Id,
						OccupancySensing::Attributes::Occupancy::Id);
		if (a == nullptr) {
			return;
		}
		esp_matter_attr_val_t val;
		attribute::get_val(a, &val);
		val.val.u8 = 1; /* OccupancyBitmap::kOccupied */
		attribute::update(auth_endpoint_id, OccupancySensing::Id,
				  OccupancySensing::Attributes::Occupancy::Id, &val);
		chip::DeviceLayer::SystemLayer().CancelTimer(auth_pulse_end, nullptr);
		(void)chip::DeviceLayer::SystemLayer().StartTimer(
			chip::System::Clock::Milliseconds32(CONFIG_HOMEKEY_AUTH_PULSE_MS),
			auth_pulse_end, nullptr);
	});
}

// Resolve the Matter user that owns the credential the reader authenticated, so
// the LockOperation event names who operated the lock (Apple Home otherwise
// notifies every device in the home). Matter task.
static app::DataModel::Nullable<uint16_t> operating_user(void)
{
	uint8_t cred[65];

	if (!ultrawidelock_reader_authenticated_credential(cred)) {
		return app::DataModel::NullNullable;
	}
	uint16_t user_index = BoltLockMgr().UserIndexForAliroCredential(ByteSpan(cred, sizeof(cred)));
	if (user_index == 0) {
		ESP_LOGW(TAG, "credential matches no stored user; LockOperation unattributed");
		return app::DataModel::NullNullable;
	}
	return app::DataModel::MakeNullable(user_index);
}

void app_aliro_unlock()
{
	CHIP_ERROR e = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
		BoltLockMgr().Unlock(door_lock_endpoint_id, DoorLock::OperationSourceEnum::kAliro,
				     operating_user());
	});
	if (e != CHIP_NO_ERROR) {
		ESP_LOGE(TAG, "unlock not scheduled: %" CHIP_ERROR_FORMAT, e.Format());
	}
}

static void relock_timer_cb(chip::System::Layer *, void *)
{
	BoltLockMgr().Lock(door_lock_endpoint_id, DoorLock::OperationSourceEnum::kAuto);
}

// BoltLockManager hook (Matter task). Momentary lock: any Unlocked, from a grant
// or a controller's Unlock command, relocks after CONFIG_HOMEKEY_RELOCK_MS.
void app_lock_state_changed(bool locked, bool aliro)
{
	ESP_LOGW(TAG, "lock state -> %s%s", locked ? "Locked" : "Unlocked", aliro ? " (Aliro)" : "");
	chip::DeviceLayer::SystemLayer().CancelTimer(relock_timer_cb, nullptr);
	if (!locked) {
		(void)chip::DeviceLayer::SystemLayer().StartTimer(
			chip::System::Clock::Milliseconds32(CONFIG_HOMEKEY_RELOCK_MS),
			relock_timer_cb, nullptr);
	}
}

/* ======================================================================
 * Reader start after commissioning (from upstream)
 * ====================================================================== */

/* Apple Home commissions twice back to back (phone fabric, then hub); wait for
 * the commissioner to go quiet before taking the BLE advertiser. */
constexpr uint32_t k_reader_start_quiet_ms = 20000;

static void reader_start_when_quiet(chip::System::Layer *layer, void *arg);

static void reader_start_after_commissioning(void)
{
	chip::System::Layer &layer = chip::DeviceLayer::SystemLayer();

	layer.CancelTimer(reader_start_when_quiet, nullptr);
	CHIP_ERROR err = layer.StartTimer(chip::System::Clock::Milliseconds32(k_reader_start_quiet_ms),
					  reader_start_when_quiet, nullptr);
	if (err != CHIP_NO_ERROR) {
		ESP_LOGE(TAG, "reader start timer failed; starting now");
		homekey_reader_start_once();
	}
}

static void reader_start_when_quiet(chip::System::Layer *layer, void *arg)
{
	(void)layer;
	(void)arg;
	chip::Server &server = chip::Server::GetInstance();

	if (server.GetCommissioningWindowManager().IsCommissioningWindowOpen() ||
	    server.GetFailSafeContext().IsFailSafeArmed()) {
		ESP_LOGI(TAG, "commissioner still busy; reader start deferred");
		reader_start_after_commissioning();
		return;
	}
	homekey_reader_start_once();
}

/* SNTP feeds only the advertisement's dynamic-tag expiry; until it syncs the advert
 * carries "expiry unavailable", which phones still resolve. On Thread it needs
 * NAT64/DNS64 from the border router. */
static void sntp_synced_cb(struct timeval *tv)
{
	(void)tv;
	ultrawidelock_ble_time_updated();
}

static void start_sntp_once(void)
{
	static bool started = false;
	if (started) {
		return;
	}
	started = true;
	esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	cfg.sync_cb = sntp_synced_cb;
	esp_err_t err = esp_netif_sntp_init(&cfg);
	ESP_LOGI(TAG, "SNTP started (%s)", esp_err_to_name(err));
}

/* ======================================================================
 * Matter callbacks
 * ====================================================================== */

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
	switch (event->Type) {
	case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
		ESP_LOGI(TAG, "Interface IP address changed");
		start_sntp_once();
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
		ESP_LOGW(TAG, "Commissioning complete");
		reader_start_after_commissioning();
		break;

	case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
		ESP_LOGW(TAG, "Commissioning failed, fail safe timer expired");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
		ESP_LOGW(TAG, "Commissioning window opened");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
		ESP_LOGW(TAG, "Commissioning window closed");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
		ESP_LOGW(TAG, "Fabric removed");
		if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
			/* The last home removed this lock: drop the reader config and revert
			 * the identity + trust store, so the old home's phones can't
			 * authenticate and the next SetAliroReaderConfig succeeds. */
			(void)UltraWideLockReaderDelegate::Instance().ClearAliroReaderConfig();
			chip::CommissioningWindowManager &commissionMgr =
				chip::Server::GetInstance().GetCommissioningWindowManager();
			if (!commissionMgr.IsCommissioningWindowOpen()) {
				CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
					chip::System::Clock::Seconds16(k_timeout_seconds),
					chip::CommissioningWindowAdvertisement::kDnssdOnly);
				if (err != CHIP_NO_ERROR) {
					ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT,
						 err.Format());
				}
			}
		}
		break;
	}

	default:
		break;
	}
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
				       uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
	ESP_LOGI(TAG, "Identify: type %u effect %u variant %u", type, effect_id, effect_variant);
	return ESP_OK;
}

// Controller writes. The wake switch is the only attribute acted on here.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
					 uint32_t cluster_id, uint32_t attribute_id,
					 esp_matter_attr_val_t *val, void *priv_data)
{
	if (type == PRE_UPDATE && !s_internal_update && endpoint_id == wake_endpoint_id &&
	    cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
		if (val->val.b) {
			homekey_wake("matter");
		} else {
			homekey_sleep("matter");
		}
	}
	return ESP_OK;
}

void app_commissioning_window_open()
{
	CHIP_ERROR sched = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
		CHIP_ERROR e = chip::Server::GetInstance()
				       .GetCommissioningWindowManager()
				       .OpenBasicCommissioningWindow();
		if (e == CHIP_NO_ERROR) {
			ESP_LOGW(TAG, "commissioning window open; pair with the printed codes");
		} else {
			ESP_LOGE(TAG, "commissioning window failed: %" CHIP_ERROR_FORMAT, e.Format());
		}
	});
	if (sched != CHIP_NO_ERROR) {
		ESP_LOGE(TAG, "commissioning window not scheduled");
	}
}

void app_print_onboarding_codes()
{
	char code[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
	const chip::RendezvousInformationFlags rendezvous(chip::RendezvousInformationFlag::kBLE);

	chip::DeviceLayer::PlatformMgr().LockChipStack();
	chip::MutableCharSpan qr(code);
	if (GetQRCode(qr, rendezvous) == CHIP_NO_ERROR) {
		char url[512];

		printf("SetupQRCode: [%s]\n", qr.data());
		if (GetQRCodeUrl(url, sizeof(url), qr) == CHIP_NO_ERROR) {
			printf("QR code URL: %s\n", url);
		}
	} else {
		printf("SetupQRCode: unavailable\n");
	}
	chip::MutableCharSpan manual(code);
	if (GetManualPairingCode(manual, rendezvous) == CHIP_NO_ERROR) {
		printf("Manual pairing code: [%s]\n", manual.data());
	} else {
		printf("Manual pairing code: unavailable\n");
	}
	chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

/* ======================================================================
 * app_main
 * ====================================================================== */

extern "C" void app_main()
{
	esp_err_t err = ESP_OK;

	board_init();
	nvs_flash_init();

	node::config_t node_config;
	node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
	ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

	/* ---- Door Lock with Aliro ---- */
	door_lock::config_t door_lock_config;
	UltraWideLockReaderDelegate::Instance().Init();
	door_lock_config.door_lock.delegate = &UltraWideLockReaderDelegate::Instance();
	door_lock_config.door_lock.lock_state = chip::to_underlying(DoorLock::DlLockState::kLocked);
	cluster::door_lock::feature::credential_over_the_air_access::config_t cota_config;
	cluster::door_lock::feature::pin_credential::config_t pin_credential_config;
	cluster::door_lock::feature::user::config_t user_config;
	endpoint_t *lock_ep = door_lock::create(node, &door_lock_config, ENDPOINT_FLAG_NONE, NULL);
	ABORT_APP_ON_FAILURE(lock_ep != nullptr, ESP_LOGE(TAG, "Failed to create door lock endpoint"));
	cluster_t *door_lock_cluster = cluster::get(lock_ep, DoorLock::Id);
	cluster::door_lock::feature::credential_over_the_air_access::add(door_lock_cluster, &cota_config);
	cluster::door_lock::feature::pin_credential::add(door_lock_cluster, &pin_credential_config);
	cluster::door_lock::feature::user::add(door_lock_cluster, &user_config);
	cluster::door_lock::feature::aliro_provisioning::add(door_lock_cluster);
	cluster::door_lock::feature::aliro_bleuwb::add(door_lock_cluster);
	/* CHIP's own auto-relock off: the momentary relock is app_lock_state_changed. */
	cluster::door_lock::attribute::create_auto_relock_time(door_lock_cluster, 0);
	door_lock_endpoint_id = endpoint::get_id(lock_ep);

	/* ---- "UWB wake" switch ---- */
	on_off_plug_in_unit::config_t wake_config;
	wake_config.on_off.on_off = false;
	endpoint_t *wake_ep = on_off_plug_in_unit::create(node, &wake_config, ENDPOINT_FLAG_NONE, NULL);
	ABORT_APP_ON_FAILURE(wake_ep != nullptr, ESP_LOGE(TAG, "Failed to create wake endpoint"));
	wake_endpoint_id = endpoint::get_id(wake_ep);

	/* ---- "homekey auth" occupancy sensor ---- */
	occupancy_sensor::config_t auth_config;
	auth_config.occupancy_sensing.occupancy_sensor_type =
		chip::to_underlying(OccupancySensing::OccupancySensorTypeEnum::kPhysicalContact);
	auth_config.occupancy_sensing.occupancy_sensor_type_bitmap =
		chip::to_underlying(OccupancySensing::OccupancySensorTypeBitmap::kPhysicalContact);
	auth_config.occupancy_sensing.feature_flags =
		cluster::occupancy_sensing::feature::other::get_id();
	endpoint_t *auth_ep = occupancy_sensor::create(node, &auth_config, ENDPOINT_FLAG_NONE, NULL);
	ABORT_APP_ON_FAILURE(auth_ep != nullptr, ESP_LOGE(TAG, "Failed to create auth endpoint"));
	auth_endpoint_id = endpoint::get_id(auth_ep);

	ESP_LOGW(TAG, "endpoints: lock %u, wake %u, auth %u", door_lock_endpoint_id,
		 wake_endpoint_id, auth_endpoint_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
	esp_openthread_platform_config_t ot_config = {
		.radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
		.host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
		.port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
	};
	set_openthread_platform_config(&ot_config);
#endif

	/* Reader comes up silent; the wake window opens advertising. */
	homekey_init();

	/* Register the reader's GATT service on Matter's NimBLE host BEFORE
	 * esp_matter::start builds the GATT table (one call, all services). */
	{
		std::vector<struct ble_gatt_svc_def> svcs;
		const struct ble_gatt_svc_def *svc =
			static_cast<const struct ble_gatt_svc_def *>(ultrawidelock_reader_ble_prepare());
		if (svc != nullptr) {
			svcs.push_back(*svc);
			CHIP_ERROR e = chip::DeviceLayer::Internal::BLEMgrImpl().ConfigureExtraServices(svcs, true);
			ESP_LOGI(TAG, "reader GATT registered: %" CHIP_ERROR_FORMAT, e.Format());
		} else {
			ESP_LOGE(TAG, "ultrawidelock_reader_ble_prepare failed; reader GATT not registered");
		}
	}

	err = esp_matter::start(app_event_cb);
	ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

	app_print_onboarding_codes();
	board_button_init();
	door_lock_init();

	/* A persisted "on" from before a reboot would otherwise read as an open window. */
	app_report_wake(false);

	if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
		ESP_LOGW(TAG, "already commissioned; starting reader");
		homekey_reader_start_once();
	}

	app_shell_start();
}
