// Shared declarations for the mfro-homekey-uwb application.
#pragma once

#include <esp_err.h>
#include <esp_matter.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

/* Matter endpoint ids, assigned in app_main. */
extern uint16_t door_lock_endpoint_id;
extern uint16_t wake_endpoint_id; /* On/Off plug-in unit: "UWB wake" */
extern uint16_t auth_endpoint_id; /* Occupancy sensor: "homekey auth" */

/** Open a basic Matter commissioning window (scheduled onto the Matter task). */
void app_commissioning_window_open();

/** printf the commissioning QR URL and manual pairing code. */
void app_print_onboarding_codes();

/** Door Lock state hook, called by BoltLockManager on the Matter task whenever the
 *  reported lock state changes. Unlocked arms the momentary relock timer. */
void app_lock_state_changed(bool locked, bool aliro);

/** Report the wake window state on the "UWB wake" On/Off endpoint. Any task. */
void app_report_wake(bool on);

/** Pulse the "homekey auth" occupancy sensor. Any task. */
void app_report_auth_pulse();

/** Momentary Aliro unlock: Door Lock -> Unlocked (attributed to the authenticated
 *  credential's user), relocked after CONFIG_HOMEKEY_RELOCK_MS. Any task. */
void app_aliro_unlock();

/** See door_lock_callbacks.cpp. */
void door_lock_init();

/** Board bring-up: XIAO RF switch, status LED. Call first in app_main. */
void board_init();
/** Status LED (wake window indicator). Any task. */
void board_led(bool on);
/** BOOT button: long press -> commissioning window, short press -> wake. */
void board_button_init();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
