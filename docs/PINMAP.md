# Harness Device — Pin Map

The Harness device is a stock **Waveshare ESP32-S3-Touch-AMOLED-1.75C** in Waveshare's own CNC aluminium case,
on a 3D-printed PLA base with a USB-C flex extension inside (CC snipped, ships with an A-to-C cable).

## Sources

| Source | Version |
|---|---|
| Waveshare schematic `hardware/schematic/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf` (**source of truth**) | `waveshareteam/ESP32-S3-Touch-AMOLED-1.75C` @ `9b295b1` (2026-11-16) |
| Waveshare `examples/arduino/libraries/board_config/pin_config.h` | same |
| Harness firmware `devices/harness-device/firmware/main/board/{pins_dial.h,board_table.c}` | `autonomous-ai/openharness` @ `7eeba50` (2026-11-18) |
| Waveshare non-C 1.75 `examples/arduino/libraries/Mylibrary/pin_config.h` (for comparison) | `waveshareteam/ESP32-S3-Touch-AMOLED-1.75` HEAD |

Local clones: `vendor/ESP32-S3-Touch-AMOLED-1.75C/`, `vendor/openharness/` (sparse, firmware only).

## Stock firmware

`vendor/ESP32-S3-Touch-AMOLED-1.75C/firmware/ESP32-S3-Touch-AMOLED-1.75C_FactoryOnly_20260114.bin`:
a merged 32 MB image, flashed at `0x0`. It restores Waveshare's out-of-box demo.

```bash
# back up the Harness firmware FIRST (32 MB)
esptool.py --chip esp32s3 -p /dev/cu.usbmodem* -b 921600 read_flash 0 0x2000000 harness-stock.bin
# then, to go stock Waveshare:
esptool.py --chip esp32s3 -p /dev/cu.usbmodem* erase_flash
esptool.py --chip esp32s3 -p /dev/cu.usbmodem* write_flash 0x0 vendor/ESP32-S3-Touch-AMOLED-1.75C/firmware/ESP32-S3-Touch-AMOLED-1.75C_FactoryOnly_20260114.bin
```

## GPIO map (from the schematic)

| GPIO | Net | Function | Harness `pins_dial.h` |
|---|---|---|---|
| 0 | GPIO0 | BOOT button (Key1, 10k pull-up) | `BSP_BOOT_BUTTON 0` ✓ |
| 1 | LCD_RESET | CO5300 reset | ✗ uses 39 (see below) |
| 2 | TP_RESET | CST9217 touch reset | ✗ uses 40 (see below) |
| 3 | SYS_OUT | PWR key state (via BSS138; reads HIGH while pressed) | not used (reads PWR via AXP2101 instead) |
| 4 | QSPI_SIO0 | AMOLED QSPI D0 | `BSP_LCD_QSPI_D0 4` ✓ |
| 5 | QSPI_SI1 | AMOLED QSPI D1 | ✓ |
| 6 | QSPI_SI2 | AMOLED QSPI D2 | ✓ |
| 7 | QSPI_SI3 | AMOLED QSPI D3 | ✓ |
| 8 | I2S_DSDIN | I2S data **ESP → ES8311** (speaker) | `BSP_I2S_DOUT 8` ✓ (comment says the opposite) |
| 9 | I2S_SCLK | I2S BCLK | `BSP_I2S_BCLK 9` ✓ |
| 10 | I2S_ASDOUT | I2S data **ES7210 → ESP** (mics) | `BSP_I2S_DIN 10` ✓ (comment says the opposite) |
| 11 | TP_INT | touch interrupt | `BSP_TOUCH_INT 11` ✓ |
| 12 | LCD_CS | AMOLED QSPI CS | `BSP_LCD_QSPI_CS 12` ✓ |
| 13 | LCD_TE | AMOLED tearing effect | not used |
| 14 | ESP32_SCL | shared I2C SCL | `BSP_I2C_SCL 14` ✓ |
| 15 | ESP32_SDA | shared I2C SDA | `BSP_I2C_SDA 15` ✓ |
| 16 | I2S_MCLK | I2S MCLK | `BSP_I2S_MCLK 16` ✓ |
| 17, 18 | GPIO17/18 | header (free) | — |
| 19 / 20 | USB_N / USB_P | native USB (Harness cable link) | — |
| 21 | QMI_INT1 | QMI8658 IMU INT1 | not used |
| 38 | QSPI_SCL | AMOLED QSPI clock | `BSP_LCD_QSPI_SCLK 38` ✓ |
| 39–42 | GPIO39–42 | header (free on the 1.75C) | 39/40 used as LCD/touch reset ✗ |
| 43 / 44 | U0TXD / U0RXD | UART0 on header | — |
| 45 | I2S_LRCK | I2S WS | `BSP_I2S_WS 45` ✓ |
| 46 | PA_CTRL | NS4150B speaker amp enable | `BSP_PA_IO 46` ✓ |

The schematic's summary table puts `QMI_INT1` on the GPIO38 row. The IMU section of the same schematic labels the net
`QMI_INT1 — GPIO21`. GPIO21 is correct: GPIO38 is QSPI SCL.

## I2C bus (GPIO15 SDA / GPIO14 SCL)

| Addr | Chip |
|---|---|
| 0x34 | AXP2101 PMIC (PWRON = PWR key, CHGLED, battery on MX1.25) |
| 0x5A | CST9217 touch |
| 0x18 | ES8311 codec (speaker out) |
| 0x40 | ES7210 ADC (dual mic, AEC loopback) |
| 0x6B | QMI8658 6-axis IMU |

## Discrepancies found

1. **Harness reset pins vs the 1.75C.** Harness `board_table.c` maps any board with a CST9217 to reset GPIO39/40.
   That is the **non-C** 1.75 wiring (`LCD_RESET 39`, `TP_RESET 40`). On the 1.75C, reset is GPIO1/2, and 39/40
   are bare header pins. The Harness firmware gives GPIO1/2 only to its CST816S board (DXQ0175Y003).
   If this unit is a 1.75C, the Harness firmware never drives the panel or touch reset, and works only because of
   power-on reset. Unverified on hardware. Check the boot log or `hello.hw`: `board_describe()` prints
   `rst=39/40` or `rst=1/2`.
2. **Harness board A ack list has no 0x6B.** The comment in `board_table.c` lists board A as acking
   `34 5A 18 40`. A 1.75C should also ack 0x6B (QMI8658). That suggests their board A was a non-C 1.75, or a
   different revision. An I2C scan of this unit settles it.
3. **Inverted I2S comments in `pins_dial.h`.** The comment on `BSP_I2S_DOUT 8` says "codec → ESP (mic data in)"
   and the comment on `BSP_I2S_DIN 10` says "ESP → codec". The schematic says the reverse. The code is correct:
   `audio_capture.c` passes `.dout = 8`, `.din = 10` to `i2s_std_config_t`, which matches the schematic and
   Waveshare's `i2s.setPins(..., DO=8, DI=10, ...)`. Only the comments are wrong.

4. **Wrong flash size.** Harness `sdkconfig.defaults` sets `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, and its
   `partitions.csv` comment says "16MB flash on the ESP32-S3-Touch-AMOLED-1.75C". This unit reports 32 MB
   (GigaDevice c8/4019), which matches Waveshare's spec; 16 MB is the non-C 1.75. The upper 16 MB is never
   used and still holds leftover Waveshare factory assets. Harness also has no assets partition, so every
   sprite and font sits inside both 8 MB OTA slots.

## Power / buttons

- **PWR key (Key2)** pulls AXP2101 `PWRON` low. The AXP2101 handles a long press as power on/off. The same net
  drives a BSS138 so the key is also readable on **GPIO3** (`SYS_OUT`, HIGH while pressed).
- **BOOT key (Key1)** is on GPIO0, with a 10k pull-up and a 100 nF debounce.
- **USB-C** (16-pin) has 5.1k Rd on CC1/CC2, so the board itself is a compliant sink. Plugged straight into a
  C-to-C source, it works. The Harness base breaks this because CC is cut in its flex.
