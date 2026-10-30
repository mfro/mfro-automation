// Interactive console (USB-Serial-JTAG): status, wake/sleep, codes, commissioning.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Register the commands and start the REPL task. Call last in app_main. */
void app_shell_start(void);

#ifdef __cplusplus
}
#endif
