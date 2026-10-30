// Console shell. Derived from ultrawidelock/apps/esp32-matter-lock/main/app_shell.cpp,
// trimmed to this app and moved to the XIAO's USB-Serial-JTAG console.
//
// Handlers run on the REPL task: CHIP state is read under the stack lock, and
// anything that mutates it is scheduled onto the Matter task.

#include <cstdlib>
#include <cstring>

#include <esp_console.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <esp_matter.h>
#include <app/server/Server.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/PlatformManager.h>

#include <ultrawidelock/reader.h>
#include <ultrawidelock/uwb.h>
#include <ultrawidelock_prim.h> // ultrawidelock_prim_init: PSA Crypto, needed by the STS KDF
#include <ultrawidelock_diag.h> // ultrawidelock_uwb_diag_on

#include "app_shell.h"
#include "app_priv.h"
#include "door_lock_manager.h"
#include "homekey.h"

int cmd_dwdiag(int argc, char **argv); /* dw_diag.cpp */

using namespace chip;
using namespace chip::app::Clusters;

static int cmd_status(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	app::DataModel::Nullable<DoorLock::DlLockState> lock_state;
	uint32_t feature_map = 0;
	uint8_t fabrics;

	DeviceLayer::PlatformMgr().LockChipStack();
	DoorLock::Attributes::LockState::Get(door_lock_endpoint_id, lock_state);
	DoorLock::Attributes::FeatureMap::Get(door_lock_endpoint_id, &feature_map);
	fabrics = Server::GetInstance().GetFabricTable().FabricCount();
	DeviceLayer::PlatformMgr().UnlockChipStack();

	printf("lock      : %s\n",
	       lock_state.IsNull() ? "unknown" : BoltLockMgr().lockStateToString(lock_state.Value()));
	printf("fabrics   : %u\n", fabrics);
	/* 0x2000 AliroProvisioning, 0x4000 AliroBLEUWB */
	printf("featuremap: 0x%04X (aliro prov %s, ble-uwb %s)\n", (unsigned)feature_map,
	       (feature_map & 0x2000) ? "y" : "n", (feature_map & 0x4000) ? "y" : "n");
	printf("heap      : %u B free, %u B largest, %u B min ever\n",
	       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
	       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
	       (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
	homekey_print_status();
	return 0;
}

static int cmd_wake(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	homekey_wake("console");
	return 0;
}

static int cmd_sleep(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	homekey_sleep("console");
	return 0;
}

static int cmd_codes(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	app_print_onboarding_codes();
	return 0;
}

static int cmd_commission(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	app_commissioning_window_open();
	app_print_onboarding_codes();
	return 0;
}

static int cmd_prov(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ultrawidelock_reader_prov_print();
	return 0;
}

static int cmd_uwbdiag(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		ultrawidelock_uwb_diag_on = true;
	} else if (argc == 2 && strcmp(argv[1], "off") == 0) {
		ultrawidelock_uwb_diag_on = false;
	} else if (argc != 1) {
		printf("usage: uwbdiag [on|off]\n");
		return 0;
	}
	printf("uwbdiag: %s\n", ultrawidelock_uwb_diag_on ? "on" : "off");
	return 0;
}

// Bring the DW3000 up the way the reader does at start (a throwaway responder
// start + stop with a zero URSK), so the wiring can be checked before
// commissioning. Idempotent: once probed the radio stays up.
static int cmd_uwbprobe(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (ultrawidelock_reader_session_active()) {
		printf("uwbprobe: a credential session is ranging; try again later\n");
		return 0;
	}
	static const uint8_t zero_ursk[32] = {0};
	struct ultrawidelock_uwb_cred_cfg cfg = {};
	cfg.channel = 9;
	cfg.sync_code_index = 9;
	cfg.slot_per_round = 1;
	cfg.ursk = zero_ursk;

	/* start_cred derives the CCC STS keys (AES via PSA) BEFORE touching the radio,
	 * and PSA refuses every operation until psa_crypto_init(). The reader does this
	 * at start; before commissioning nothing has. Idempotent. */
	if (ultrawidelock_prim_init() != 0) {
		printf("uwbprobe: PSA crypto init FAILED\n");
		return 0;
	}

	esp_log_level_set("*", ESP_LOG_INFO); /* show the DEV_ID / probe lines */
	int prev_diag = ultrawidelock_uwb_diag_on;
	ultrawidelock_uwb_diag_on = 1; /* radio bring-up trace (DIAGK) */
	int rc = ultrawidelock_uwb_start_cred(&cfg);
	if (rc == 0) {
		ultrawidelock_uwb_stop();
	}
	ultrawidelock_uwb_diag_on = prev_diag;
	esp_log_level_set("*", ESP_LOG_WARN);
	printf("uwbprobe: %s (rc=%d)\n",
	       rc == 0 ? "DW3000 up" : "FAILED: check power jumper, SPI wiring, mode-0 straps",
	       rc);
	return 0;
}

static int cmd_factoryreset(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("factory reset: erasing and rebooting\n");
	/* esp_matter::factory_reset() erases only Matter's NVS; the reader identity +
	 * trust store would survive and the old home's phones could still get in. */
	ultrawidelock_reader_provision_clear();
	esp_matter::factory_reset();
	return 0;
}

static int cmd_log(int argc, char **argv)
{
	static const struct {
		const char *name;
		esp_log_level_t level;
	} levels[] = {
		{"none", ESP_LOG_NONE}, {"error", ESP_LOG_ERROR}, {"warn", ESP_LOG_WARN},
		{"info", ESP_LOG_INFO}, {"debug", ESP_LOG_DEBUG}, {"verbose", ESP_LOG_VERBOSE},
	};

	if (argc == 3) {
		for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
			if (strcmp(argv[2], levels[i].name) == 0) {
				esp_log_level_set(argv[1], levels[i].level);
				printf("log: %s -> %s\n", argv[1], levels[i].name);
				return 0;
			}
		}
	}
	printf("usage: log <tag|*> <none|error|warn|info|debug|verbose>\n");
	return 0;
}

void app_shell_start(void)
{
	esp_console_repl_t *repl = NULL;
	esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
	repl_cfg.prompt = "homekey> ";
	/* uwbprobe runs the DW3000 bring-up (dwt_probe/initialise) on this task. */
	repl_cfg.task_stack_size = 8192;

	esp_console_dev_usb_serial_jtag_config_t dev_cfg =
		ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl));

	const esp_console_cmd_t cmds[] = {
		{.command = "status", .help = "lock, fabrics, heap, wake window, range", .hint = NULL,
		 .func = cmd_status},
		{.command = "wake", .help = "open the wake window (as the Matter switch does)",
		 .hint = NULL, .func = cmd_wake},
		{.command = "sleep", .help = "close the wake window", .hint = NULL, .func = cmd_sleep},
		{.command = "codes", .help = "print the commissioning QR URL + manual code",
		 .hint = NULL, .func = cmd_codes},
		{.command = "commission",
		 .help = "open a commissioning window (also: long-press BOOT 5 s)", .hint = NULL,
		 .func = cmd_commission},
		{.command = "prov", .help = "print the reader identity / provisioning state",
		 .hint = NULL, .func = cmd_prov},
		{.command = "uwbprobe", .help = "bring up the DW3000 and report (wiring check)",
		 .hint = NULL, .func = cmd_uwbprobe},
		{.command = "dwdiag",
		 .help = "bit-banged DW3000 wiring/power diagnostic (reboot afterwards)",
		 .hint = NULL, .func = cmd_dwdiag},
		{.command = "uwbdiag", .help = "uwbdiag [on|off]: raw per-frame UWB trace",
		 .hint = NULL, .func = cmd_uwbdiag},
		{.command = "log", .help = "log <tag|*> <level>", .hint = NULL, .func = cmd_log},
		{.command = "factoryreset", .help = "erase Matter + reader state and reboot",
		 .hint = NULL, .func = cmd_factoryreset},
	};
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
	}
	ESP_ERROR_CHECK(esp_console_register_help_command());

	const esp_app_desc_t *app = esp_app_get_description();
	printf("\n%s %s - 'help' lists commands\n\n", app->project_name, app->version);
	ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
