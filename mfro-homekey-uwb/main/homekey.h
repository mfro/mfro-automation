// Aliro (Apple Home Key) reader with a wake window.
//
// Idle: the reader is attached to Matter's NimBLE host but its credential
// advertisement is gated off, so phones have nothing to connect to and range.
// homekey_wake() opens a window (CONFIG_HOMEKEY_WAKE_WINDOW_S): advertising on,
// approach controller armed. The first trusted UWB range inside
// CONFIG_HOMEKEY_UNLOCK_CM grants: momentary Door Lock unlock + Wallet
// "unlocked" + auth sensor pulse. The window then closes (advertising off, links
// dropped) shortly after the momentary relock, or at timeout.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Before esp_matter::start: close the advertising gate so the reader comes up
 *  silent. */
void homekey_init(void);

/** Start the reader task once (after commissioning has gone quiet, or at boot
 *  when already commissioned). Idempotent. */
void homekey_reader_start_once(void);

/** Open (or extend) the wake window. Any task. @p why is logged. */
void homekey_wake(const char *why);

/** Close the wake window early. Any task. */
void homekey_sleep(const char *why);

/** True while the wake window is open. */
bool homekey_window_open(void);

/** Milliseconds left in the open window, 0 when closed. */
int32_t homekey_window_remaining_ms(void);

/** printf a few lines of reader / window state (console `status`). */
void homekey_print_status(void);

#ifdef __cplusplus
}
#endif
