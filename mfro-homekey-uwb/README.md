# mfro-homekey-uwb

Apple Home Key with **UWB approach unlock** on a Seeed XIAO ESP32-C6 + Qorvo
DWM3000EVB. Matter over Thread, Home Assistant integration via Matter multi-admin.
Built on the [ultrawidelock](../../ultrawidelock) ESP32 components (Aliro reader,
DW3000 UWB engine, NimBLE transport).

There is no bolt. A grant is an *event*: the Door Lock reports Unlocked for 2 s,
then Locked again, and the "homekey auth" sensor pulses. Home Assistant
automations do the rest.

See [PLAN.md](PLAN.md) for the design, phases and open risks.

## Behaviour

| State | What happens |
|---|---|
| Idle | Aliro BLE advertising **off** (phones never connect or range), DW3000 idle, Thread router up |
| Wake | Turn on the **UWB wake** switch (HA / Apple Home), single-press BOOT, or `wake` on the console: advertising on for 30 s |
| Grant | First trusted UWB range ≤ 100 cm (approach-filtered): Door Lock → Unlocked (attributed to the key's user; Wallet shows the unlock), **homekey auth** occupied for 1 s, relock after 2 s |
| Close | After grant + relock, or after 30 s: advertising off, links dropped, wake switch → off |

Matter endpoints (what HA shows):

| Endpoint | Device type | HA entity |
|---|---|---|
| 1 | Door Lock (Aliro Provisioning + BLE-UWB) | `lock` (momentary) |
| 2 | On/Off Plug-in Unit "UWB wake" | `switch`: turn on to open the window |
| 3 | Occupancy Sensor "homekey auth" | `binary_sensor`: pulses on each grant |

Tunables are in `menuconfig` under **mfro-homekey-uwb** (window length, unlock
distance, relock time, pulse length, LED/button GPIOs).

## Wiring (XIAO ESP32-C6 → DWM3000EVB Arduino header)

| XIAO pin | GPIO | Signal | EVB pin |
|---|---|---|---|
| D8 | 19 | SCLK | D13 |
| D9 | 20 | MISO | D12 |
| D10 | 18 | MOSI | D11 |
| D7 | 17 | CS | D10 |
| D2 | 2 | IRQ | D8 |
| D1 | 1 | RSTn | D7 |
| D0 | 0 | WAKEUP | D9 |
| 3V3 | | power | 3V3 |
| GND | | ground | GND |
| GND | | SPI mode straps | D1 (SPI-POL) and D0 (SPI-PHA) |

- Power the EVB from **3V3, never 5 V**. Check the EVB's **power-select jumper**:
  if it's on the wrong source, SPI fails silently (ultrawidelock
  `docs/esp32-gotchas.md` §2.1 cost them days).
- RSTn is open-drain: no external pull-up.
- Don't use GPIO3/GPIO14 (the XIAO's RF switch, driven by the firmware), GPIO9
  (BOOT), GPIO15 (user LED, the wake-window indicator) or GPIO12/13 (USB).
- Pins are set in `sdkconfig.defaults` (`CONFIG_ULTRAWIDELOCK_DW3000_PIN_*`).

## Toolchain

- ESP-IDF **v5.5.4** via `eim` (activation script
  `~/.espressif/tools/activate_idf_v5.5.4.sh`, source at
  `/mnt/rocket/code/esp/v5.5.4/esp-idf`).
- esp-matter at **`93b1680`** in `/mnt/rocket/code/esp/esp-matter`, installed with
  `./install.sh --no-host-tool` and a Python ≥ 3.11 on PATH during bootstrap
  (connectedhomeip needs it; the system 3.10 fails on `mobly`).
- ultrawidelock checkout at `../../ultrawidelock`.

These match upstream's validated pair. ESP-IDF v6 is not supported by that
esp-matter revision.

The Makefile defaults to those paths. Override with `IDF_ACTIVATE=`,
`ESP_MATTER_PATH=`, `ULTRAWIDELOCK_PATH=`.

## Build / flash

```sh
make build        # first build compiles connectedhomeip: ~10+ min
make flash        # XIAO USB-C (USB-Serial-JTAG); PORT=/dev/ttyACM0 if needed
make monitor      # ctrl-] quits
make go           # all three
make fullclean    # after editing sdkconfig.defaults
```

Image: ~1.9 MB in a 3.75 MB factory partition (no OTA on the 4 MB part).

## First boot: hardware check (Phase 1)

With the EVB wired, flash and run `uwbprobe` on the console. It brings the DW3000
up the way the reader does at start and prints the probe/DEV_ID lines:

```
homekey> uwbprobe
...
uwbprobe: DW3000 up (rc=0)
```

If it fails, check in this order: power-select jumper, CS/SCLK/MOSI/MISO, mode-0
straps. After commissioning, the reader probes the radio itself at start.

## Commissioning

1. `make monitor`: the boot log (and the `codes` command) prints the QR URL and
   manual pairing code. These are CHIP's test values, the same on every board, and
   Home will warn that the accessory is uncertified.
2. **Apple Home first** (it provisions the Aliro key): Home → Add Accessory → scan
   the QR, keep the phone next to the board. It commissions over BLE and joins your
   Thread network through the HomePod/Apple TV.
3. Wait for the Home Key to appear in Wallet. The reader starts ~20 s after
   commissioning goes quiet.
4. **Home Assistant**: in Apple Home → the lock → Accessory Settings → *Turn On
   Pairing Mode*, then HA → Settings → Devices → Add Matter device → enter the code
   Home shows. HA must use the same Thread network (it does).
5. In HA, automate: *something* → turn on `switch.uwb_wake`; `binary_sensor.homekey_auth`
   (or the lock's Unlocked state) → open the door.

Long-press BOOT for 5 s, or run `commission` on the console, to open a basic
commissioning window. `factoryreset` wipes Matter state and the reader trust store.

## Latency

Every grant prints a budget (WARN level, always visible):

```
homekey: homekey-lat: wake->open 3 ms, open->grant 4938 ms, total 4941 ms
ultrawidelock-lat: BLE_CONNECT+0 GATT_SPSM_READ+.. ... NEAR_DWELL+.. ms
```

The second line is the reader's per-phase trace, +ms from the phone's BLE
connect, so `open->grant` minus `NEAR_DWELL` is how long the phone took to find
and connect to the reader. `log homekey info` adds every UWB range (trusted or
still building consensus) with its time since the window opened.

Knobs, slowest to fastest to try:

| Knob | Where | Effect |
|---|---|---|
| `HOMEKEY_NEAR_DWELL` (default 1) | menuconfig | in-range medians required after the 3-block trust consensus; each is ~180-200 ms |
| Relaxed BLE interval | automatic | a phone that rejects 15 ms is asked for 15-30 ms instead of being left at 40 ms (`peer rejected 15 ms` in the log) |
| `ULTRAWIDELOCK_RSSI_GATE=n` | sdkconfig.defaults | ranging starts as soon as the phone connects instead of when it's within about 1 m, so trust builds during the walk; phone power is bounded by the wake window anyway. Enables `HOMEKEY_APPROACH_PREDICT` |
| `HOMEKEY_APPROACH_PREDICT` | menuconfig | grants ~`HOMEKEY_PREDICT_LEAD_MS` before a walking key reaches the unlock distance |
| `ULTRAWIDELOCK_RANGE_TRUST_K` | sdkconfig.defaults | consecutive agreeing blocks before a range is trusted (default 3). Lowering it weakens the relay/spoof guard, so only change it deliberately |

The second walk-up onward should use the Aliro fast flow (`expedited-FAST ...
AUTH1 skipped` / `flow.fast`), which drops one signed round trip.

## Console

`status`, `wake`, `sleep`, `codes`, `commission`, `prov`, `uwbprobe`, `uwbdiag on|off`,
`log <tag> <level>` (the default level is WARN; `log homekey info` shows ranges),
`factoryreset`.

## Changes made to ultrawidelock

Edited directly in the checkout:

- `ports/esp32/components/ultrawidelock_uwb/{Kconfig,port/board_pins.h}`:
  `CONFIG_ULTRAWIDELOCK_DW3000_CUSTOM_PINS` lets a project supply its own DW3000
  pin map (default off, so upstream targets are unchanged).
- `modules/ultrawidelock_cred/{include/ultrawidelock_ble.h,src/ultrawidelock_ble_nimble.c}`:
  `ultrawidelock_ble_set_adv_enabled()` gates the credential advertisement and drops
  reader links when closed (default open, which is upstream's behaviour).
- `tests/ports/esp32/test_esp_ultrawidelock_ble.c`: tests for the gate.
  `make check` in ultrawidelock passes (10338/0).
