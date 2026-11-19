# ESP32-S3-Touch-AMOLED-1.75C Firmware Flashing and Erasing

This product provides test firmware that can be flashed directly to verify whether the onboard devices are functioning properly.

- Factory firmware download: [ESP32-S3-Touch-AMOLED-1.75C Factory Recovery Firmware (260114)](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/releases/download/v1.0.1/ESP32-S3-Touch-AMOLED-1.75C-FactoryOnly-260114.bin). Factory images are identified by `FactoryOnly` in the filename on the Releases page.
- Flash address: `0x00`

Prebuilt examples are available from [ESP32-S3-Touch-AMOLED-1.75C Firmware Releases](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/releases). Extract the selected `*-combined.zip` package and run `flash_combined.bat COMx` on Windows or `./flash_combined.sh /dev/ttyACM0` on Linux after installing esptool with `python -m pip install esptool`. The combined binary can also be flashed at `0x00`. For split binaries, use the offsets in that package's manifest.

- Firmware Flashing
- Firmware Erasing

The following steps apply to the firmware provided on this page.

- Download and extract Espressif's official Flash Download Tool ([Download](https://dl.espressif.com/public/flash_download_tool.zip))

- Run Flash Download Tool and select the options that match the development board's MCU and download interface. The screenshot below uses ESP32-S3 and USB as an example; use the options required by the product's hardware design.

  ![Firmware flashing step 1](images/Flash-86b5bb03edf9280baa095ae4b86227f5.webp)

- Parameter settings

  - Select the COM port for the development board
  - Set BAUD to the maximum value, 1152000
  - Click the "..." button in the row, select the bin file provided by Waveshare, manually enter the flash address given at the top of this page in the field to its right, and select the leftmost checkbox in the row
  - Click **START** to begin flashing

  ![Firmware flashing step 2](images/Flash-upload-01-f72b49ebd39c4aa330bff5ebb002e375.webp)

- Wait for flashing to complete (this may take some time; please be patient)

- Press the reset button and verify the result

  ![Firmware flashing step 3](images/Flash-upload-02-a46b6c8206fb3ff64920eccbcd6f5261.webp)

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTIgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTYuNSAwQzMuNDggMCAxIDIuMTkgMSA1YzAgLjkyLjU1IDIuMjUgMSAzIDEuMzQgMi4yNSAxLjc4IDIuNzggMiA0djFoNXYtMWMuMjItMS4yMi42Ni0xLjc1IDItNCAuNDUtLjc1IDEtMi4wOCAxLTMgMC0yLjgxLTIuNDgtNS01LjUtNXptMy42NCA3LjQ4Yy0uMjUuNDQtLjQ3LjgtLjY3IDEuMTEtLjg2IDEuNDEtMS4yNSAyLjA2LTEuNDUgMy4yMy0uMDIuMDUtLjAyLjExLS4wMi4xN0g1YzAtLjA2IDAtLjEzLS4wMi0uMTctLjItMS4xNy0uNTktMS44My0xLjQ1LTMuMjMtLjItLjMxLS40Mi0uNjctLjY3LTEuMTFDMi40NCA2Ljc4IDIgNS42NSAyIDVjMC0yLjIgMi4wMi00IDQuNS00IDEuMjIgMCAyLjM2LjQyIDMuMjIgMS4xOUMxMC41NSAyLjk0IDExIDMuOTQgMTEgNWMwIC42Ni0uNDQgMS43OC0uODYgMi40OHpNNCAxNGg1Yy0uMjMgMS4xNC0xLjMgMi0yLjUgMnMtMi4yNy0uODYtMi41LTJ6IiAvPjwvc3ZnPg==)tip

If the tool remains at "Waiting for power-on sync", hold down **BOOT** and power cycle the device to enter download mode.

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTIgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTYuNSAwQzMuNDggMCAxIDIuMTkgMSA1YzAgLjkyLjU1IDIuMjUgMSAzIDEuMzQgMi4yNSAxLjc4IDIuNzggMiA0djFoNXYtMWMuMjItMS4yMi42Ni0xLjc1IDItNCAuNDUtLjc1IDEtMi4wOCAxLTMgMC0yLjgxLTIuNDgtNS01LjUtNXptMy42NCA3LjQ4Yy0uMjUuNDQtLjQ3LjgtLjY3IDEuMTEtLjg2IDEuNDEtMS4yNSAyLjA2LTEuNDUgMy4yMy0uMDIuMDUtLjAyLjExLS4wMi4xN0g1YzAtLjA2IDAtLjEzLS4wMi0uMTctLjItMS4xNy0uNTktMS44My0xLjQ1LTMuMjMtLjItLjMxLS40Mi0uNjctLjY3LTEuMTFDMi40NCA2Ljc4IDIgNS42NSAyIDVjMC0yLjIgMi4wMi00IDQuNS00IDEuMjIgMCAyLjM2LjQyIDMuMjIgMS4xOUMxMC41NSAyLjk0IDExIDMuOTQgMTEgNWMwIC42Ni0uNDQgMS43OC0uODYgMi40OHpNNCAxNGg1Yy0uMjMgMS4xNC0xLjMgMi0yLjUgMnMtMi4yNy0uODYtMi41LTJ6IiAvPjwvc3ZnPg==)tip

Click **ERASE** to erase the entire flash. If erasing fails or the device does not respond, hold down **BOOT** and power cycle the device to enter download mode, then try again.

- Download and extract Espressif's official Flash Download Tool ([Download](https://dl.espressif.com/public/flash_download_tool.zip))

- Run Flash Download Tool and select the options that match the development board's MCU and download interface. The screenshot below uses ESP32-S3 and USB as an example; use the options required by the product's hardware design.

  ![Firmware erasing step 1](images/Flash-86b5bb03edf9280baa095ae4b86227f5.webp)

- Select the COM port for the device, **do not select any bin file**, and click **START**

  ![Firmware erasing step 2](images/Flash-erase-01-85de25c2a8c81037ac3df6329684707e.webp)

- When the operation above is complete, click **ERASE** to erase the flash

  ![Firmware erasing step 3](images/Flash-erase-02-d98a792c050c4f556c9f4fb246ee578e.webp)

- Wait for erasing to complete

  ![Firmware erasing step 4](images/Flash-upload-02-a46b6c8206fb3ff64920eccbcd6f5261.webp)

[![](data:image/svg+xml;base64,PHN2ZyBjbGFzcz0iZmVlZGJhY2stYnV0dG9uX19pY29uIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld2JveD0iMCAwIDI0IDI0IiBmaWxsPSJub25lIiBzdHJva2U9ImN1cnJlbnRDb2xvciIgc3Ryb2tlLXdpZHRoPSIyIiBzdHJva2UtbGluZWNhcD0icm91bmQiIHN0cm9rZS1saW5lam9pbj0icm91bmQiIGFyaWEtaGlkZGVuPSJ0cnVlIj48cGF0aCBkPSJNMjEgMTVhMiAyIDAgMCAxLTIgMkg3bC00IDRWNWEyIDIgMCAwIDEgMi0yaDE0YTIgMiAwIDAgMSAyIDJ6IiAvPjwvc3ZnPg==)Give Feedback](https://docs.google.com/forms/d/e/1FAIpQLSfayJEZ5J-dp-3Wq_dkWsVRhNuQ6C79_GNV62mYuFW8Mj6U8Q/viewform)
