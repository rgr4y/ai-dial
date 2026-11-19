# Firmware options: Harness dial (Waveshare ESP32-S3-Touch-AMOLED-1.75C)

Checked 2026-11-18 against ESPHome 2026.10.2 (latest release), `esphome/esphome` dev, and openharness @ `7eeba50`.
Firmware paths are relative to `vendor/openharness/devices/harness-device/firmware/`.

## TL;DR

Use ESPHome. Every chip except the AXP2101 has core support, and the AXP2101 has a maintained external component. The one board-specific
bug (reset on GPIO39/40 instead of GPIO1/2) is also in the ESPHome preset, and you fix it with a pin override.
Nothing in the Harness firmware needs porting except a few constants and one touch quirk.

## ESPHome coverage for this board

| Chip | Support | Notes |
|---|---|---|
| CO5300 466x466 QSPI | **core** `mipi_spi`, `model: WAVESHARE-ESP32-S3-TOUCH-AMOLED-1.75` | Preset in [`mipi_spi/models/waveshare.py`](https://github.com/esphome/esphome/blob/dev/esphome/components/mipi_spi/models/waveshare.py): 466x466, 16-bit, `offset_width: 6`, CS 12, **`reset_pin: 39`**, requires PSRAM. On the 1.75C, override with `reset_pin: GPIO1`. The offset of 6 matches Harness (`main/ui/habitat/display_habitat.c:443`). Use bus `spi: type: quad`, clk 38, data 4–7. Brightness is the [`mipi_spi` brightness light](https://esphome.io/components/display/mipi_spi/). |
| CST9217 touch @0x5A | **core** [`cst9220`](https://esphome.io/components/touchscreen/cst9220/) (#16888) | The docs list CST9217 explicitly. In source, the default address is 0x5A, and setup accepts chip ID `0x9217` as well as `0x9220` (`cst9220_touchscreen.h:14-15`). Use `reset_pin: GPIO2`, `interrupt_pin: GPIO11`, `transform: {mirror_x: true, mirror_y: true}` (180°, no swap). This matches Harness `board_table.c:36` → `touch_habitat.c:47` and the devices.esphome.io YAML. You no longer need the `shelson/esphome-cst9217` external component that the devices page still pulls. |
| AXP2101 | **external** [`m5stack/esphome-yaml` → `components/axp2101`](https://github.com/m5stack/esphome-yaml/tree/main/components/axp2101) | Not in core, which has no `axp*` component. M5Stack's component is XPowersLib-based and was pushed 2026-11-18. Its sensor platform exposes `battery_level`, `battery_voltage`, `axp_temperature` and `battery_charging` (binary). It has no PWR-key IRQ. Its rail control targets M5 boards, so check what its `setup()` writes before you trust it on this board. [`stefanthoss/esphome-axp2101`](https://github.com/stefanthoss/esphome-axp2101) was last pushed 2025-12, is Core2-only, and its README calls it partial. |
| PWR key | **core** `binary_sensor: gpio`, GPIO3 | The key reads HIGH while pressed (PINMAP.md), so you don't need the PMIC IRQ. The AXP2101 still handles long-press power-off in hardware. |
| QMI8658 @0x6B | **core** [`qmi8658`](https://esphome.io/components/motion/qmi8658/) | INT1 is on GPIO21. |
| ES8311 / ES7210 | **core** | Closed. Core supports one shared `full_duplex: true` bus ([#19959](https://github.com/esphome/esphome/pull/19959)). It is in **2026.11.0b1, not 2026.10.x**: before 2026.11, the mic and speaker take turns on the bus lock. Pins: MCLK **16** (the non-C devices page uses 42, which is wrong here), BCLK 9, WS 45, DOUT 8, DIN 10, PA 46. Dual mic/AEC: core `es7210` has `tdm:`, but the core `i2s_audio` mic can't receive TDM ([es7210 docs](https://esphome.io/components/audio_adc/es7210/)). AEC means an external component, for example `esp_aec`/`esp_afe` in [n-IA-hane/esphome-intercom](https://github.com/n-IA-hane/esphome-intercom). I haven't evaluated those. |

Existing YAML: [devices.esphome.io: Waveshare ESP32-S3-Touch-AMOLED-1.75](https://devices.esphome.io/devices/waveshare-esp32-s3-touch-amoled-175/)
covers the **non-C** board only: reset 39/40, MCLK 42, 16 MB flash, external CST9217 component. No 1.75C page exists. The 1.75C
deltas are reset 1/2, MCLK 16, 32 MB flash, plus the QMI8658 and the GPIO3 key.

## Transport for agent state, no Home Assistant

| Option | Fit |
|---|---|
| Native API + [`aioesphomeapi`](https://esphome.io/components/api/) from your host agent | Best fit. Use `api: actions:` with typed string/int/arrays for push, entities for touch/button events back. Set `reboot_timeout: 0s`: the default is 15 min, and the device reboots if no client connects. `homeassistant.event/action` need HA, so don't use them for device→host traffic; use entity state instead. |
| MQTT | Works if you already run a broker. Least code on the host. |
| `web_server` / `http_request` | REST in either direction. Fine for low-rate state. |
| USB | I'd skip it. Core has `tinyusb` + `usb_cdc_acm` (S3 supported), but TinyUSB takes over GPIO19/20 from USB-Serial/JTAG, so logging and flashing over that port change. The docs don't say how (I checked: tinyusb/usb_cdc_acm pages are silent). It also has no documented read/write API, so you'd write a lambda/external component. Wi-Fi is much less work. |

## Harness firmware: what's worth taking

The firmware has 46k lines, but 20k are `focus_faces.c` and 6k are `fonts.c`. It is bespoke ESP-IDF C. The display path is **not LVGL**:
the LVGL UI was ripped out for a custom text compositor called "habitat" (`main/CMakeLists.txt:1-10`). Every UI module is wired to
the cable protocol (83 `cable_`/`audio_client` references in `ui_habitat.c`). The code is defensive and heavily commented
(render-stall guard, task WDT panic, OTA rollback), but it's all built around their daemon.

| Piece | Verdict |
|---|---|
| Reset pins | **Bug, confirmed in code.** Any board with a CST9217 gets 39/40 (`board/board_table.c:33-36`), and the default before detection is also 39/40 (`board/board.c:17`). |
| CO5300 init sequence (`display_habitat.c:61-76`) | Reference only. ESPHome's CO5300 init is shorter (`amoled.py:98-114`). If the panel misbehaves, diff the two. Harness runs at 40 MHz QSPI with an x-gap of 6. |
| CST9217 stale-ACK handling (`touch_habitat.c:95-107`) | **The one thing worth porting.** Harness says the CST9217 returns stale ACKs while idle and treats `ESP_ERR_INVALID_RESPONSE` as "no change". Core `cst9220` just does `status_comm_result()` on the read. If you see phantom releases or comm warnings, this is the fix: a small patch or external override. |
| Touch mirror flag | Already covered by `transform:`. |
| Full-duplex I2S init (`audio_capture.c:200-225`) | No longer needed: core full-duplex does the same job. Mic gain 37.5 dB (`audio_capture.c:274`) and leaving ES7210 mic select at default (`:241-243`) are useful constants. Their capture is mono 16 kHz with no AEC. |
| AXP2101 PWR-key IRQ (`board/power.c:13-37`) | Not needed: GPIO3 covers the key. It's a reference if you ever write an AXP component (PONKEY press edge = 0x41/0x49 bit 1). |
| `pins_dial.h` | The I2S DOUT/DIN comments are inverted (lines 36-37); the values are right. `partitions.csv` assumes 16 MB, and its comment still mentions Wi-Fi/pairing, which is stale. `ptt.c` comments still mention LVGL. Ignore all of it. |
| Cable protocol, OTA-over-cable, pets, habitat UI, fonts, voice turn handling | Useless to you. These are protocol-specific (`cable_client.c:1220-1395`, magic `A5 48`), and ESPHome OTA plus your own transport replace them. |

## Alternatives, briefly

- **Plain ESP-IDF + LVGL** (fork Waveshare `examples/esp-idf/02_lvgl_demo_v9`): worth it only if you need AEC/AFE or USB transport
  done properly, or frame-perfect UI. It costs far more than ESPHome for a status dial.
- **Strip Harness**: no. You'd keep the bugs and throw away its UI, which is most of the code.
- **Arduino**: no advantage over the two above.

## Recommendation

ESPHome. Display, touch, IMU and audio are all core. Battery comes from one external component, and the PWR key needs no component.
Write your agent-status UI in `lvgl:` and drive it from your host over the native API via `aioesphomeapi`, or over MQTT.

First steps:
1. Back up the 32 MB flash (command in PINMAP.md). Then flash a minimal ESPHome config: `mipi_spi` model `WAVESHARE-ESP32-S3-TOUCH-AMOLED-1.75`
   with `reset_pin: GPIO1`, `esp32: flash_size: 32MB`, octal PSRAM, `i2c: scan: true` on 15/14. Confirm the scan finds 0x34/0x5A/0x18/0x40/0x6B.
2. Add `cst9220` (reset GPIO2, int GPIO11, mirror x+y), the GPIO3 key, `qmi8658`, and the M5Stack `axp2101` battery sensors. Log touch
   for a few minutes to see whether the stale-ACK quirk shows up.
3. Add `api:` with `reboot_timeout: 0s` and one `actions:` entry (for example `set_agent_state(name, status, question)`).
   Drive it from a 20-line `aioesphomeapi` script before you build the UI.

## Not verified

- No hardware test of anything above: the GPIO1/2 reset, M5's `axp2101` on this board, and whether CST9217 stale ACKs hit `cst9220`.
- How TinyUSB interacts with USB-Serial/JTAG logging in ESPHome.
- The intercom repo's AEC components.
