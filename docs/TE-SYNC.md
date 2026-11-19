# TE sync for the CO5300 (mipi_spi fork)

> The fork was removed on 2026-11-19 and is in git history at commit c7e03b5.

> **NOT IN USE (2026-11-18).** `esp32-dial.yaml` is back on core `mipi_spi`, with no
> `external_components` local entry, no `te_pin`, and LVGL `buffer_size: 25%`. No TE variant beat the
> no-TE baseline (about 21fps, slight tearing). See the results table below.
> `components/mipi_spi/` is kept for reference only. To re-enable it, add the local
> `external_components` entry and `te_pin: GPIO13`.

`components/mipi_spi/` is a copy of ESPHome 2026.10.0's core `mipi_spi`. It overrides core through
`external_components` (`source: {type: local, path: components}`, `components: [mipi_spi]`). Core
`mipi` is still used as-is. Every change is marked `TE patch`.

## What changed

- `display.py`: optional `te_pin` (internal input pin) and `te_timeout` (default 20ms).
- `mipi_spi.h`:
  - `te_setup_()` runs at the end of `setup()`. It sends TEON `0x35 0x00` and attaches a
    rising-edge ISR on `te_pin`.
  - `te_wait_()` sits in `write_to_display_()` after the address window and before the pixel write.
    It waits only if this write would cross the panel scan (see Design).
  - `te_done_()` collects stats and logs one INFO line every 5s:
    `TE 59.1Hz | 234 chunks, 112 waits avg 8046us, 0 timeouts | write 27us per 466px row | 0 crossed scan`.
    `crossed scan` is the estimated number of writes that tore inside the chunk.
- `mipi_spi.cpp`: the ISR `TeSync::gpio_intr`. It timestamps the edge (`esp_timer_get_time`) and
  gives a binary semaphore.
- `esp32-dial.yaml`: the `external_components` entry, plus `te_pin: GPIO13` on `disp`.

Where TEON `0x35 0x00` comes from:
- Espressif `esp_lcd_co5300` (`~/workspace/esp-iot-solution/.../esp_lcd_co5300_mipi.c:147`).
- Zephyr `display_co5300.c:60`.
- The Harness init table (`display_habitat.c:68`).

Param `0x00` is MIPI DCS set_tear_on mode 0 (V-blank only). Zephyr's binding and driver treat TE as
active-high and trigger on edge-to-active, so the rising edge marks the start of V-blank. **No CO5300
datasheet is on disk**: the opcode and edge come from three drivers that agree, and from the measured
59Hz TE on GPIO13. Neither Waveshare's examples nor Harness use the TE pin. Harness sends TEON, and
Arduino_GFX has it commented out.

## Timing (measured on the device)

| | value |
|---|---|
| TE period | 59.1Hz, 16.9ms |
| Scan line time | 16.9ms / (466 + porch) ≤ **36µs** |
| Pixel write, 466px row at 80MHz QSPI | **26–28µs** (theory: 932B / 40MB/s = 23.3µs, plus per-4092B transaction overhead) |
| Flush chunks per LVGL refresh | ~2.7 (a 300x300 box is 90k px; the 25% buffer is 54k px, so 2 chunks, plus labels) |

Writes outrun the scan by about 1.3x. A chunk started while the scan is above it stays ahead and
can't tear. A chunk started while the scan is below it lands entirely in the next panel frame, also
untorn. It tears only when the scan is inside the chunk, or would reach it during the write.

The SPI write is blocking (`spi_device_polling_start`/`_end` in 4092-byte transactions), so the
flush returns only after the pixels are out. LVGL's `update_when_display_idle` is off by default and
unused here. It checks `Component::is_idle()` and doesn't interact with this patch.

## Design: wait only when needed (scan-aware, per chunk)

Three variants, each flashed and measured with the cube moving:

| variant | fps | chunks crossing the scan |
|---|---|---|
| Before (no TE, Rob's baseline) | ~21 | not measured |
| Wait for TE before **every** chunk (what Zephyr does) | 13–14 | 0 |
| Wait for TE **once per refresh** (`LV_EVENT_REFR_START` hook) | 16–19 | **56–69 per 5s** (≈1 in 3 later chunks tear) |
| **Per chunk, scan-aware (the code in the fork)** | **16–17** | **0** |
| Scan-aware, plus LVGL `buffer_size: 100%` in PSRAM (one flush per refresh) | 13–16 (brief motion only) | 0 |

The PSRAM buffer slows pixel writes to **43–45µs per 466px row**, from 27µs with the internal-SRAM
buffer. That's slower than the ≤36µs scan, so a single burst started at TE gets overtaken by the scan
unless the dirty box starts low on the screen. In that run 26 of 32 chunks still waited, about 10ms
each.

Per-refresh doesn't work here. LVGL renders chunk 2 after flushing chunk 1. That takes about 10ms,
during which the scan moves on, so later chunks often start with the scan inside them. Waiting before
every chunk is tear-free, but it costs about 9ms per chunk, roughly 2.7 times a frame.

The fork's gate estimates the current scan line from the last TE edge and the measured period,
then predicts the write duration from the measured µs/px. It waits for TE only when the scan would be
inside the chunk during the write. About half the chunks wait.

Ceiling: the estimate uses line = period / 466 and ignores the vertical porch, so it runs a few lines
behind the real scan. `TE_MARGIN = 32` lines absorbs that. Chunks starting within 32 rows of the top
always wait. Until the first 5s stats window ends, every chunk waits.

**What TE can't fix:** with several chunks per refresh, chunk 1 can land in panel frame N and chunk 2
in frame N+1. For one 17ms frame the cube is then new above the chunk boundary and old below it. Each
chunk is clean, but the seam between them can show. The only fix is one flush per refresh, meaning an
LVGL buffer that holds the whole dirty box (~180KB for 300x300). That's above the 25% internal-SRAM
buffer, and going to PSRAM costs render speed. Not changed here.

## Disable

Remove `te_pin:` from the display. The patch then compiles to no-ops. To drop the fork entirely,
remove the local `external_components` entry as well.

## Rebase on a newer ESPHome

```sh
CORE=$(python3 -c 'import esphome.components.mipi_spi as m, os; print(os.path.dirname(m.__file__))')  # use the esphome venv's python
OLD=/tmp/mipi_spi-2026.10.0   # pristine 2026.10.0 copy: pip download esphome==2026.10.0, or git
diff -ru -x __pycache__ "$OLD" components/mipi_spi > /tmp/te.patch       # the TE patch
rsync -a --delete -x --exclude __pycache__ "$CORE/" components/mipi_spi/  # new upstream
patch -p1 -d components/mipi_spi < /tmp/te.patch                         # reapply; fix rejects by hand
grep -rn "TE patch" components/mipi_spi                                  # every hunk should still be there
```

Baseline is ESPHome **2026.10.0**. The hunks touch `setup()` (one line after `reset_params_()`) and
`write_to_display_()` (one line before `enable()`, one after `disable()`). The rest is appended
members and functions. If upstream ever adds TE support, drop the fork.
