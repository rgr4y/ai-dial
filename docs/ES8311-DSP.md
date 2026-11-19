#e for https://github.com/m5stack/esphome-yaml.git@None, will refresh on the next run after 22h 49min (refresh: 1d); use refresh: always to update now
WARNING GPIO45 is a strapping pin and should only be used for I/O with care.
Attaching external pullup/down resistors to strapping pins can cause unexpected failures.
See https://esphome.io/guides/faq/#why-am-i-getting-a-warning-about-strapping-pins
WARNING GPIO45 is a strapping pin and should only be used for I/O with care.
Attaching external pullup/down resistors to strapping pins can cause unexpected failures.
See https://esphome.io/guides/faq/#why-am-i-getting-a-warning-about-strapping-pins
WARNING GPIO46 is a strapping pin and should only be used for I/O with care.
Attaching external pullup/down resistors to strapping pins can cause unexpected failures.
See https://esphome.io/guides/faq/#why-am-i-getting-a-warning-about-strapping-pins
WARNING GPIO3 is a strapping pin and should only be used for I/O with care.
Attaching external pullup/down resistors to strapping pins can cause unexpected failures.
See https://esphome.io/guides/faq/#why-am-i-getting-a-warning-about-strapping-pins
WARNING 'ota' password wastes significant flash and RAM; using 'encryption' instead is recommended - see https://esphome.io/components/ota/esphome/#configuration-variables ES8311 DAC DRC + EQ for the dial speaker

2026-11-18. Target: Waveshare ESP32-S3-Touch-AMOLED-1.75C, ESPHome 2026.10.0, ES8311 @ 0x18 on `bus_a`, 44.1 kHz / 16-bit mono, ES8311 → NS4150B → tiny speaker.

**TL;DR**
- **DRC:** usable now. The bit layout is verified against the Everest user guide. The highest ceiling it offers is **-6.0 dBFS**; nothing between -6 and 0 is available. It is an AGC with a dead band (cuts above MAX, boosts below MIN, holds between), not a lookahead limiter. Once it is on, the volume register **becomes the DRC's maximum gain**.
- **EQ:** the register layout is verified. The **coefficient number format is not**: Everest documents it only through a non-public tool, and Espressif's own driver hard-disables the DAC EQ with `// TODO: Fix noise when enable is true`. The snippet ships with EQ off. To get it working, follow the calibration in §4.
- **Your volume ceiling is already +1 dB of digital gain** (0.76 → `0x32 = 0xC1`, verified from source). Does near-0 dBFS material then clip inside the DAC? Probably, but I haven't verified what the ES8311 does above 0 dBFS or the stream's real peak level. See §1.
- **OpenHarness does nothing clever:** a -14.7 dBFS 2 kHz square-wave beep, and no speech playback, EQ, DRC or volume curve (§5).

---

## 1. Volume arithmetic (do this first, it costs nothing)

`ES8311::set_volume()` writes `0x32 = remap<uint8_t>(v, 0..1, 0..255)`, which truncates (`es8311.cpp:66-70`, `core/helpers.h:884-886`). `0xBF` = 0 dB, at 0.5 dB/step (UG p29, Reg 0x32).

| media volume | 0x32 | gain |
|---|---|---|
| 0.60 (your `volume_min`) | 0x99 (153) | -19.0 dB |
| 0.75 | 0xBF (191) | 0.0 dB |
| 0.76 (your `volume_max`) | 0xC1 (193) | **+1.0 dB** |

The speaker media_player's own `volume_min`/`volume_max` default to 0/1 (`speaker/media_player/__init__.py:144-145`), so your script's value goes straight through, and the i2s speaker hands volume to the DAC with no software gain (`i2s_audio_speaker.cpp:178-181`, `speaker.h:74-81`). A modern master peaks at about -0.1 dBFS, plus MP3 intersample overs. At the top of the slider those peaks get +1 dB of gain. Does that clip digitally? It's likely, but I haven't verified the ES8311's behaviour above 0 dBFS (saturate or wrap) or the stream's actual peaks. **Cheapest fix: `volume_max: "0.75"`** (0.75 × 255 = 191.25 → 0xBF; note that 0.749 truncates to 0xBE, -0.5 dB). You lose 1 dB.

## 2. Register map, 0x31–0x43 (verified)

Primary source is the **ES8311 User Guide Rev 1.11 (Sept 2018)**, pages 28–31: <https://github.com/tuhde/Periph/blob/main/datasheets/adc_dac/es8311.pdf>. It is cross-checked against ES8311 DS rev 8.0, pp. 27–28 (<https://github.com/Quicr/hactar/blob/main/hardware/datasheets/es8311-ds.pdf>), mainline Linux `sound/soc/codecs/es8311.{c,h}`, and Espressif `esp-audio-dev/esp_codec_dev/device/es8311/es8311_{drc,eq}.c`. The Espressif-hosted `Audio_ES8311.pdf` has **no register map**; it is the 5-page electrical datasheet.

| Reg | Bits | Field | Values | Sources |
|---|---|---|---|---|
| 0x31 | 7 / 6 / 5 | DSMMUTE_TO / DSMMUTE / DEMMUTE | 1 = mute | UG p29; Linux es8311.h:132-134 |
| 0x32 | 7:0 | DAC_VOLUME | 0x00 -95.5 dB … 0xBF 0 dB … 0xFF +32 dB, 0.5 dB/step. **With DRC on, this becomes the DRC max gain.** | UG p29 |
| 0x33 | 7:0 | DAC_OFFSET | not used here | DS rev8 |
| 0x34 | 7 | DRC_EN | 1 = enable (default 0) | UG p30; Linux es8311.h:139; Espressif es8311_drc.c:50 (`reg \| 0x80`) |
| 0x34 | 3:0 | DRC_WINSIZE | `n` → "0.25dB/2^(n+1) LRCK" (0 = /2 … 15 = /65536) | UG p30; Linux es8311.c:59-62,113-115 |
| 0x35 | 7:4 | DRC_MAXLEVEL | see level table | UG p30; Linux es8311.h:142; Espressif es8311_drc.c:25-30 |
| 0x35 | 3:0 | DRC_MINLEVEL | see level table | same |
| 0x36 | – | – | undocumented in UG and DS rev8; leave it alone | |
| 0x37 | 7:4 | DAC_RAMPRATE | "DAC VC/DRC ramp rate": 0 = soft ramp off (default), `n` = 0.25 dB / 2^(n+1) LRCK | UG p28-29; Linux es8311.h:147 |
| 0x37 | 3 | DAC_EQBYPASS | **1 = bypass**, 0 = EQ on. Reset value 0x08 = bypassed. | UG p31; Linux es8311.c:179-180 (`invert=1`); Espressif es8311_eq.c:54 |
| 0x37 | 2:0 | – | undocumented. Read-modify-write. | |
| 0x38–0x3B | | DACEQ_B0[29:24], [23:16], [15:8], [7:0] | 30-bit, **MSB at the lowest address**, 0x38 uses bits 5:0 | UG p31 |
| 0x3C–0x3F | | DACEQ_B1, same layout | | UG p31 |
| 0x40–0x43 | | DACEQ_A1, same layout | | UG p31 |

Byte order cross-check: Espressif writes the LSB to the `_BASE` address and then decrements, with bases 0x3B, 0x3F and 0x43 (`es8311_reg.h:90-92`, `es8311_eq.c:63-66`). NRL-ESP32 writes `(c>>24)&0x3F` to 0x38 and then increments (`src/app/driver/es8311.cpp:443-449`). Both agree with the UG. (The UG lists 0x3C/0x40 bits as 7:0 rather than 5:0, an internal inconsistency. Mask to 6 bits anyway.)

**DRC level table** (UG p30). It is exactly 20·log10((n+1)/32): Linux TLV `es8311.c:67-75` and Espressif `target_table` (`es8311_drc.c:25-30`) both match.

| n | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dBFS | -30.1 | -24.1 | -20.6 | -18.1 | -16.1 | -14.5 | -13.2 | -12.0 | -11.0 | -10.1 | -9.3 | -8.5 | -7.8 | -7.2 | -6.6 | **-6.0** |

**What the ESPHome driver writes at setup** (`es8311.cpp:22-49`): reset (0x00 = 0x1F, 0x00), clocks, format, mic, `0x32` via `set_volume(0.75)`, then 0x0D, 0x0E, 0x12, 0x13, 0x1C = 0x6A, **0x37 = 0x08** (ramp off, EQ bypassed), and 0x00 = 0x80. It never touches 0x34, 0x35 or 0x38–0x43 again. After setup it only writes 0x32 (volume) and 0x31 (mute) (`es8311.cpp:66-70, 222-237`). An `on_boot` write therefore sticks. The Waveshare driver does the same thing: `0x37 = 0x08` at `vendor/.../07_ES8311/es8311.c:346`, and fade is a read-modify-write of 0x37[7:4] at `:410-416`.

## 3. DRC: how it actually behaves, and settings

From UG p29-30, verbatim in substance: *"If DRC is enabled, the DAC_VOLUME is inactive and DAC_VOLUME is used for the maximum gain of DRC… it automatically detects the maximum volume of digital signal in window size time controlled by DRC_WINSIZE. If the maximum volume is higher than the target maximum level, it will turn down the volume … in 0.25dB/step with a ramp rate controlled by DAC_RAMPRATE. If the maximum volume is lower than the target minimum level, it will turn up the volume … with the same ramp rate."*

What that means in practice:
- **Peak detection** is over a window. The gain cuts above MAX, rises below MIN, and holds in between. Attack and release share **one** ramp rate (0x37[7:4]).
- **Ceiling floor is -6.0 dBFS.** You can't get a "-1 to -3 dBFS limiter". If the amp clips somewhere between -6 and 0 dBFS output, -6 fixes it. If it clips lower, it doesn't.
- **The volume slider becomes the gain cap.** If the DRC measures after the gain (unverified), then at the bottom of your range (0x99, -19 dB) the signal never reaches -6 dBFS and nothing changes from today, while at the top loud passages get pulled down to -6 dBFS. Raising `volume_max` would then give compression into a -6 dBFS ceiling instead of clipping.
- **Possible slider stall (unverified, from the cut/boost/hold reading).** After the DRC has cut, gain only rises again while the window peak is below MIN. Turning the slider up while music plays at MIN to MAX level may therefore do nothing audible until a quiet passage. A narrower band (MIN closer to MAX) means less stall but more hunting. The sweep in §6 checks this.
- **Never set MIN = 0 (-30.1).** Gain recovers only when the window peak drops below MIN, so after one transient it would stay cut until near-silence. Keep MIN within about 6 dB of MAX.
- **Turning it off:** NRL-ESP32 reports that *"a nonzero winsize with DRC disabled correlated with latched DAC distortion in the field"* (`es8311.cpp:417-423`). That is anecdotal and unverified, but cheap to respect: when disabling, write 0x34 = 0x00 and 0x35 = 0x00.

**Timing math at 44.1 kHz.** One LRCK is 22.68 µs, and step `n` = 2^(n+1) LRCK.

| n | LRCK | time | as ramp (0.25 dB per step) |
|---|---|---|---|
| 2 | 8 | 0.18 ms | 1.38 dB/ms |
| 3 | 16 | 0.36 ms | 0.69 dB/ms → 6 dB in 8.7 ms |
| 4 | 32 | 0.73 ms | 0.34 dB/ms |
| 7 | 256 | 5.8 ms | – |
| 8 | 512 | 11.6 ms | – |
| 9 | 1024 | 23.2 ms | – |

**Proposed starting point:**
- `0x34 = 0x88` (EN, WINSIZE 8). If WINSIZE is the detection window, as the prose says, 11.6 ms spans at least one full cycle down to 86 Hz, so the detector won't chatter inside a single bass cycle. That chatter would cause exactly the intermodulation you're trying to kill.
- `0x35 = 0xF7` (MAX -6.0, MIN -12.0).
- `0x37[7:4] = 3` (0.69 dB/ms). This is fast enough to catch a transient within about 10 ms. It also soft-ramps your volume changes, which is a side benefit.

**Unverified (the UG doesn't say):**
- Whether WINSIZE is the detection window or a rate. The prose says window; the table text reads "0.25dB/2LRCK", copied from the ramp table.
- What DRC does with RAMPRATE = 0, which is what ESPHome leaves. The snippet sets it to non-zero so this doesn't matter.
- Whether the level is measured before or after the 0x32 gain.

All three show up in the tone test in §6.

## 4. EQ: 1st-order high-pass at 150–200 Hz

UG p30: *"ES8311 DAC has an equalizer which is a 1st filter. This equalizer can be programmed as low pass filter or high pass filter… Everest can provide a tool to calculate the coefficient."* Three coefficients (B0, B1, A1) fit a single first-order section.

**Not verified anywhere** (UG, DS rev8, Linux, Espressif, NRL, Periph): the fixed-point scaling, signedness, and the sign convention of A1. Periph's spec says outright that coefficients *"come from Everest's calculation tool (not public)"*. The only explicit claim I found ("Q1.29") is a third-party comment about the **ADC** EQ (`YONGHU-YUAN/cardputer-studio studio/ES8311Audio.h:19`) with no source behind it. Espressif ships the DAC EQ forced off with a "noise" TODO (`es8311_eq.c:52-53`).

The UG also says the internal ADC clock-to-LRCK ratio must be ≥256 with the EQ on (UG clock section). For the DAC side this is unstated. ESPHome's 44.1 kHz row (`es8311_const.h:153`, 11.2896 MHz = 256·fs, ÷1, ÷1) gives exactly 256, so it would pass anyway.

**Math.** Bilinear 1st-order HPF: K = tan(π·fc/fs), b0 = 1/(1+K), b1 = -b0, a1 = (K-1)/(K+1), H(z) = (b0 + b1·z⁻¹)/(1 + a1·z⁻¹).

| fc | K | b0 | a1 (in 1 + a1·z⁻¹) | 50 Hz | 100 Hz | 150 Hz | 300 Hz | 1 kHz |
|---|---|---|---|---|---|---|---|---|
| 150 | 0.010686 | 0.989427 | -0.978854 | -10.0 | -5.1 | -3.0 | -1.0 | -0.1 dB |
| 180 | 0.012824 | 0.987339 | -0.974678 | -11.5 | -6.3 | -3.9 | -1.3 | -0.1 dB |
| 200 | 0.014249 | 0.985952 | -0.971903 | -12.3 | -7.0 | -4.4 | -1.6 | -0.2 dB |

A first-order slope is only 6 dB/oct, which is gentle. It takes the edge off 50–100 Hz and doesn't remove it.

**Encodings, 30-bit two's complement.** "A1 as-is" means the hardware computes y = b0x + b1x₋₁ − A1·y₋₁. "A1 negated" means y = … + A1·y₋₁.

| fc | format | B0 | B1 | A1 as-is | A1 negated |
|---|---|---|---|---|---|
| 150 | Q2.28 | 0x0FD4B14A | 0x302B4EB6 | 0x30569D6C | 0x0FA96294 |
| 150 | Q1.29 | 0x1FA96294 | 0x20569D6C | 0x20AD3AD7 | 0x1F52C529 |
| 180 | Q2.28 | 0x0FCC23CD | 0x3033DC33 | 0x3067B865 | 0x0F98479B |
| 180 | Q1.29 | 0x1F98479B | 0x2067B865 | 0x20CF70CB | 0x1F308F35 |
| 200 | Q2.28 | 0x0FC67534 | 0x30398ACC | 0x30731597 | 0x0F8CEA69 |
| 200 | Q1.29 | 0x1F8CEA69 | 0x20731597 | 0x20E62B2E | 0x1F19D4D2 |

**Calibration.** It takes about 10 minutes. Run it at low volume, with a 1 kHz tone and then a 100 Hz tone, measuring with a phone SPL app or the ES7210.
1. Write all coefficients to 0 and clear the bypass bit. You should get **silence**. This proves the EQ is in the path and that writes land.
2. Set B1 = A1 = 0 and B0 = `0x08000000`, then compare the level against bypass. -6 dB means Q2.28, -12 dB means Q1.29, and 0 dB means Q3.27.
3. Keep B0 = 0.5 and B1 = 0 from step 2, set A1 = +0.5 in that format (Q2.28: `0x08000000`), and compare the 1 kHz level against **step 2**. Comparing bass to treble doesn't work: the pole only shapes the top octave, so 100 Hz and 1 kHz land within about 0.2 dB of each other either way. If the hardware does `1 + A1·z⁻¹`, gain is 0.5/1.5, about 3.5 dB below step 2, so use **A1 as-is**. If it does `1 − A1·z⁻¹`, gain is 0.5/0.5, about 6 dB above step 2, so use **A1 negated**. Neither case exceeds 0 dB, so this is safe.
4. Load the matching row and listen for the "noise" Espressif hit.

## 5. OpenHarness (`vendor/openharness/devices/harness-device/firmware/main/`)

There are no DSP tricks. The speaker is used only for a notification beep. Nothing anywhere in the harness-device tree writes 0x34, 0x35, 0x37 or 0x38+.
- **What they play:** `audio_capture.c:55-73`. Three 80 ms bursts of a **~2 kHz square wave**, amplitude **6000/32767 (-14.7 dBFS)**, with 60 ms gaps, at **16 kHz** (`audio_capture.h:21`). That is well above any speaker resonance, at low level.
- **Volume:** `esp_codec_dev_set_out_vol(s_spk, 100)` at `audio_capture.c:86`. The default curve maps 100 to 0 dB (`esp_codec_dev.c:79-90`). `es_cfg` leaves `hw_gain` zeroed (`audio_capture.c:139-145`), so `esp_codec_dev_col_calc_hw_gain` defaults to PA 5 V / DAC 3.3 V and returns 20·log10(3.3/5) = -3.6 dB (`esp_codec_dev_vol.c:50-61`). The driver then subtracts it (`es8311.c:359-362`). Net result: **0x32 ≈ 0xC6 (+3.5 dB)**, which with a -14.7 dBFS tone peaks at about -11 dBFS. These line numbers are from esp-adf HEAD, whose manifest says 1.6.2, the same version their `dependencies.lock` pins.
- **PA:** `.pa_pin = BSP_PA_IO` (GPIO46) at `audio_capture.c:143`. The codec raises it on open and drops it on close, so the amp is off between beeps. Opening also writes 0x37 = 0x08 (`es8311.c:328`).
- **harness-pro** (`main/audio/audio_capture.c:117-150`, fetched via gh) uses the same beep and also sets vol 100.
- **Takeaway:** their answer to distortion is to never play anything loud or bass-heavy. Nothing there is transferable beyond "keep the PA off when idle". I didn't check whether your `speaker_amp` switch already does that.

## 6. Snippet

Add this as another item under `esphome: on_boot:`. The ES8311 sets up at `DATA` priority (600, the Component default), so anything below that runs after `setup()`. `write_byte`/`read_byte` are public on `I2CDevice` (`i2c/i2c.h:133-268`). `id(dac)` with no trailing `.` renders as the Pvariable pointer (`core/__init__.py:320`, `cpp_generator.py:889-892`).

```yaml
    - priority: -100
      then:
        - lambda: |-
            // ES8311 DAC DRC + optional EQ. See docs/ES8311-DSP.md. ESPHome never rewrites these after setup().
            static const char *const TAG = "es8311_dsp";
            auto *codec = id(dac);
            auto wr = [codec](uint8_t reg, uint8_t val) {
              if (!codec->write_byte(reg, val)) ESP_LOGE(TAG, "write 0x%02X=0x%02X failed", reg, val);
            };

            // ---- knobs ----
            const bool    DRC_ON  = true;
            const uint8_t DRC_WIN = 8;     // 0x34[3:0]: 2^(n+1) LRCK; 8 = 11.6 ms @ 44.1k
            const uint8_t DRC_MAX = 15;    // 0x35[7:4]: 15 = -6.0 dBFS (the highest the chip offers)
            const uint8_t DRC_MIN = 7;     // 0x35[3:0]: 7 = -12.0 dBFS; keep within ~6 dB of MAX
            const uint8_t RAMP    = 3;     // 0x37[7:4]: 0.25 dB / 2^(n+1) LRCK; 3 = 0.69 dB/ms (DRC + volume)
            const bool    EQ_ON   = false; // coefficient format UNVERIFIED: run doc §4 calibration first
            // 180 Hz HPF, Q2.28, "A1 as-is" variant. Swap per calibration result (doc §4 table).
            const uint32_t EQ[3] = {0x0FCC23CD, 0x3033DC33, 0x3067B865};  // B0, B1, A1

            // EQ coefficients go in while bypass is still set (zeros = silence if unbypassed first).
            if (EQ_ON) {
              for (int i = 0; i < 3; i++) {
                const uint8_t base = 0x38 + 4 * i;  // MSB first: [29:24] [23:16] [15:8] [7:0]
                wr(base + 0, (EQ[i] >> 24) & 0x3F);
                wr(base + 1, (EQ[i] >> 16) & 0xFF);
                wr(base + 2, (EQ[i] >> 8) & 0xFF);
                wr(base + 3, EQ[i] & 0xFF);
              }
            }

            uint8_t r37 = 0x08;
            if (!codec->read_byte(0x37, &r37)) ESP_LOGE(TAG, "read 0x37 failed; assuming 0x08");
            r37 = (r37 & 0x07) | ((RAMP & 0x0F) << 4) | (EQ_ON ? 0x00 : 0x08);  // keep undocumented 2:0
            wr(0x37, r37);

            // NRL-ESP32 field note: nonzero winsize with DRC off correlated with latched distortion -> zero both.
            wr(0x35, DRC_ON ? ((DRC_MAX & 0x0F) << 4) | (DRC_MIN & 0x0F) : 0x00);
            wr(0x34, DRC_ON ? (0x80 | (DRC_WIN & 0x0F)) : 0x00);

            uint8_t v34 = 0, v35 = 0, v37 = 0;
            codec->read_byte(0x34, &v34); codec->read_byte(0x35, &v35); codec->read_byte(0x37, &v37);
            ESP_LOGI(TAG, "0x34=0x%02X 0x35=0x%02X 0x37=0x%02X (EQ %s)", v34, v35, v37, EQ_ON ? "on" : "bypassed");
```

**Test (≈5 min)**
1. Run a null test with a 0 dBFS 1 kHz sine. Play it with `DRC_ON = false` at media volume 0.703 (0x32 = 0xB3, -6 dB) and note the SPL; this stays below the amp-clip regime. Then play it with `DRC_ON = true` at `volume_max`. Equal SPL means the -6 dBFS ceiling works. Don't use DRC-off at `volume_max` as the baseline: the amp is clipping there, so the measured delta reads short.
2. If the DRC-on reading is louder, the DRC isn't engaging. Try RAMP or WINSIZE; otherwise the level is being measured before the 0x32 gain.
2b. With DRC on and music playing, sweep the slider up and then down. Check whether the top end responds (see the stall note in §3).
3. Play a 60 Hz sine at 0 dBFS. The level should hold steady. If you hear warble, the detector is chattering, so raise `DRC_WIN` to 9 or 10.
4. Then play the blues stream and A/B the distortion.

## Couldn't verify
- The snippet body compiles and emits the expected writes in both the DRC-on/EQ-off and DRC-off/EQ-on paths, but only against a g++ stub of `write_byte`/`read_byte`. It has not been through an ESPHome build or run on hardware.
- DACEQ fixed-point format and A1 sign. §4 calibrates these empirically.
- Why Espressif disabled the DAC EQ ("noise").
- WINSIZE semantics (window vs rate), DRC behaviour with RAMPRATE = 0, and whether DRC measures before or after the 0x32 gain. These also decide whether the slider stalls.
- Whether the ES8311 saturates or wraps above 0 dBFS internally, and the stream's actual peak level.
- The NS4150B gain and clip point relative to ES8311 dBFS on this board, so whether -6 dBFS is enough headroom. Measure it.
- NRL's "latched distortion" anecdote.
