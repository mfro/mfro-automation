# mfro-homekey-uwb — implementation plan

## Status

| Phase | State |
|---|---|
| 0 toolchain + skeleton | **Done.** IDF v5.5.4 + esp-matter `93b1680` installed under `/mnt/rocket/code/esp`; project builds for esp32c6 (1.88 MB image, 52% of the 3.75 MB partition free; ~216 KB DIRAM left for heap). |
| 1 UWB radio bring-up | Ready for hardware: wire per README, flash, `uwbprobe`. |
| 2 Matter/Thread + Aliro provisioning | Code in place (door lock + Aliro delegate, Thread FTD, BLE kept after commissioning). Needs commissioning on hardware. |
| 3 walk-up unlock | Skipped as a separate step: the wake-window loop is the upstream approach loop with a bounded window, so it's tested with `wake` / the button. |
| 4 wake window + grant event | Code in place: advertising gate (ultrawidelock patch + host tests), On/Off "UWB wake", Occupancy "homekey auth", momentary lock. Needs hardware. |
| 5 polish | Partly: console, BOOT button (long press = commissioning window, click = wake), Kconfig tunables. |

UWB Apple Home Key reader on a XIAO ESP32-C6 + DWM3000EVB, built on
`../../ultrawidelock` (local path), integrated with Home Assistant over Matter.

## Decisions

| Topic | Decision |
|---|---|
| Key protocol | Matter Door Lock + Aliro (the only route to a UWB Wallet key). HAP / HK-HomeKit-Lib dropped; device is re-added in Home. |
| Host framework | Native ESP-IDF + esp-matter, forked from `ultrawidelock/apps/esp32-matter-lock`. No ESPHome. |
| HA integration | Matter multi-admin. Apple Home commissions first (it provisions the Aliro key). Then open a commissioning window and add the lock to HA's Matter integration. Same Thread network for both. |
| Network | Matter over Thread, mains powered, so the lock is a Thread router (FTD) and always reachable. Fallback: Wi-Fi. |
| Board | Seeed XIAO ESP32-C6 (4 MB flash, no PSRAM), DWM3000EVB on SPI2 via the GPIO matrix. No other board options (the RPis are not a target). |
| Wake | From HA only (no GPIO trigger). |
| Grant output | An event to HA only. No relay, and no persistent lock/unlock state. |
| NFC | None. |
| Scope cuts (PoC) | No satellite anchor (`NUM_RESPONDERS=1`), no Approach Direction cluster, no BLE DFU, no PIV, no HA-MQTT, no Matter OTA; console shell kept for bring-up. |
| ultrawidelock source | By path (`ULTRAWIDELOCK_PATH`, default `../../ultrawidelock`); our changes live as patches in `patches/`. |
| Toolchain | ESP-IDF **v5.5.x** (upstream validates v5.5.4) + esp-matter `93b1680`, installed via `eim` next to the existing v6.1. See "Toolchain" below. |

## Target behaviour

1. **Idle:** DW3000 asleep, Aliro BLE advertising off, so the phone has no reason
   to connect or range. Matter/Thread stays up as a router.
2. **Wake:** an HA automation turns on the "UWB wake" switch, a Matter On/Off endpoint
   that HA shows as a `switch`. That wakes the DW3000, starts Aliro advertising, and
   starts the reader/approach loop.
3. **Window:** up to 30 s (Kconfig). The first trusted range ≤ 1.0 m from the approach
   engine is a **grant**.
4. **Grant →**
   - Door Lock goes to Unlocked with the operating user, which emits the
     LockOperation event and Reader-Status-Changed that Wallet needs for its unlock
     animation; then straight back to Locked after ~2 s. The lock is never left
     Unlocked, so this is momentary and stateless.
   - An "authenticated" occupancy-sensor endpoint pulses for 1 s, which HA shows as a
     `binary_sensor` for automations (the equivalent of today's `homekey auth`).
5. **End of window** (grant or timeout): stop advertising, drop any BLE session,
   DW3000 back to sleep, and set the wake switch to off so HA sees that the window closed.

HA entities expected: `lock` (momentary, read-only in practice), `switch` (UWB wake),
`binary_sensor` (homekey auth). Apple Home will also show the switch and sensor; they
can be hidden there.

## Wiring (XIAO ESP32-C6 ↔ DWM3000EVB Arduino header)

| XIAO pin | GPIO | Signal | EVB pin | Notes |
|---|---|---|---|---|
| D8 | 19 | SCLK | D13 | same SPI pins as the current PN532 build |
| D9 | 20 | MISO | D12 | |
| D10 | 18 | MOSI | D11 | |
| D7 | 17 | CS | D10 | UART0 RX pad, free: console is USB-Serial-JTAG |
| D2 | 2 | IRQ | D8 | |
| D1 | 1 | RSTn | D7 | open-drain: drive low or release, no pull-up to another rail |
| D0 | 0 | WAKEUP | D9 | |
| 3V3 / GND | | power | 3V3 / GND | EVB from 3V3, **not** 5 V; check the EVB power-select jumper |
| — | | SPI-POL / SPI-PHA | D1 / D0 → GND | mode-0 straps |

Avoid: GPIO3/14 (XIAO RF switch, which also rules out upstream's C6 IRQ pin), GPIO9
(BOOT), GPIO15 (LED/strap), GPIO12/13 (USB). D3–D6 are left spare.

## Toolchain

- Use ESP-IDF v5.5.x, not v6.1. The pinned esp-matter (`93b1680`) and the ultrawidelock
  ESP32 port were built and validated on v5.5.4. v6 removes or splits legacy driver
  components (`ultrawidelock_uwb` `REQUIRES driver`) and changes the NimBLE/mbedTLS
  defaults the port's sdkconfig relies on. Not worth fighting in a PoC.
- Install v5.5.4 with `eim`, and clone esp-matter at `93b1680` with submodules, under
  `~/esp/`. The project Makefile takes `IDF_EXPORT` / `ESP_MATTER_PATH` overrides.
  The connectedhomeip checkout is large (~10+ GB with submodules); only the esp32
  platform submodules are needed.
- Revisit v6 after the PoC works.

## Flash budget (4 MB)

Upstream's C5/C6 Matter image (~2.0–2.1 MB) overflows the 4 MB layout's 1.9 MB OTA
slots. The PoC drops OTA and uses a single large factory app partition (~3.5 MB).
Wi-Fi is compiled out (`CONFIG_ENABLE_WIFI_STATION=n`), which offsets most of
OpenThread's size. Updates go over USB. Matter OTA could return later with
`-Os`/LTO trimming if it fits.

## Phases

### Phase 0: toolchain + skeleton
- Install IDF v5.5.4 + esp-matter `93b1680`.
- Project layout: `CMakeLists.txt` (adds `${ULTRAWIDELOCK_PATH}/ports/esp32/components`
  and the esp-matter dirs), `main/` (trimmed copy of `esp32-matter-lock/main`),
  `sdkconfig.defaults`, `partitions.csv`, `Makefile`, `patches/`.
- Patch 1: make `board_pins.h` honour overrides (`#ifndef ULTRAWIDELOCK_DW3000_PIN_*`),
  set from our project. Small and upstreamable.
- Done when it builds for esp32c6 and fits in flash.

### Phase 1: UWB radio bring-up (no Matter)
- Build ultrawidelock's `examples/esp32/reader` for C6 with our pins (patch 1).
- Done when the DW3000 gives a valid `DEV_ID` and `app_responder_start() = 0`.

### Phase 2: Matter over Thread + Aliro provisioning
- sdkconfig: OpenThread FTD, Wi-Fi off, BLE kept after commissioning
  (`USE_BLE_ONLY_FOR_COMMISSIONING=n`), 802.15.4/BLE coexistence on, NimBLE L2CAP CoC
  + 16 KB host stack, `FREERTOS_HZ=1000`, SPI without DMA (as upstream).
- Keep `UltraWideLockReaderDelegate`, `door_lock_manager`, door-lock callbacks, and the
  extra-GATT registration. Reader always on for now.
- Done when Apple Home commissions over BLE → Thread and a Home Key appears in Wallet.
  Then add it to HA through multi-admin and check that the `lock` entity appears.
- **Risk gate:** check that the 802.15.4 + BLE + UWB slot timing holds (§6.10).

### Phase 3: walk-up unlock, always on
- Upstream reader task / approach loop unchanged. The goal is to prove an end-to-end
  approach unlock on this hardware before adding gating.
- Measure: DW3000 IRQ→worker latency (`DIAG_HOT`), ARM FAIL rate, free and minimum heap.
- If the timing fails on Thread: try lowering BLE/Thread activity during the window,
  then try Wi-Fi.

### Phase 4: HA wake window + grant event
- Patch 2 (`modules/ultrawidelock_cred`): `ultrawidelock_reader_adv_enable(bool)`, which
  stops/starts Aliro advertising and the dynamic-tag refresh callout and terminates an
  open L2CAP session on disable.
- DW3000 sleep/wake around the window (`dwt_configuresleep` + WAKEUP pin), re-init on wake.
- Matter endpoints: On/Off ("UWB wake", turning it on starts the window, and it reports
  off when the window ends); Occupancy Sensor ("homekey auth", 1 s pulse on grant).
- Momentary Door Lock unlock → relock on grant; auto-relock timer disabled.
- 30 s window timer; on grant or timeout, back to idle.

### Phase 5: polish
- Commissioning-window trigger for adding HA (console command plus a long press on
  the BOOT button).
- Kconfig for the unlock distance, window length and relock delay.
- Optional: last distance as a Matter attribute, and a deny event.

### Later
Proximity relock inside the window, an ESPHome wrapper, a satellite anchor, Matter
OTA, IDF v6.

## Risks
- C6 + DWM3000EVB not hardware-validated upstream; Thread + BLE + UWB on one C6 never
  tested, and the single core means UWB timing competes with both radios (§6.10).
- 4 MB flash: the image must fit without OTA.
- Internal RAM: the C6 has 512 KB and no PSRAM. Upstream's S3 ran short with Matter +
  the reader (Wi-Fi). Thread instead of Wi-Fi should help.
- How HA's Matter integration presents the On/Off and Occupancy endpoints and the
  lock events: verify in Phase 2/4. If the lock entity is awkward for automations, the
  occupancy sensor is the primary signal.
