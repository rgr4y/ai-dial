# Working with ESP-IDF

This chapter includes the following sections, please read as needed:

- [Setting Up Development Environment](#esp-idf-setup)
- [Example](#example)

## Setting up the Development Environment

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTQgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTcgMi4zYzMuMTQgMCA1LjcgMi41NiA1LjcgNS43cy0yLjU2IDUuNy01LjcgNS43QTUuNzEgNS43MSAwIDAgMSAxLjMgOGMwLTMuMTQgMi41Ni01LjcgNS43LTUuN3pNNyAxQzMuMTQgMSAwIDQuMTQgMCA4czMuMTQgNyA3IDcgNy0zLjE0IDctNy0zLjE0LTctNy03em0xIDNINnY1aDJWNHptMCA2SDZ2Mmgydi0yeiIgLz48L3N2Zz4=)info

Use ESP-IDF v5.5.5 or v6.0.2 for the current ESP32-S3-Touch-AMOLED-1.75C examples.

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTQgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTYuMyA1LjY5YS45NDIuOTQyIDAgMCAxLS4yOC0uN2MwLS4yOC4wOS0uNTIuMjgtLjcuMTktLjE4LjQyLS4yOC43LS4yOC4yOCAwIC41Mi4wOS43LjI4LjE4LjE5LjI4LjQyLjI4LjcgMCAuMjgtLjA5LjUyLS4yOC43YTEgMSAwIDAgMS0uNy4zYy0uMjggMC0uNTItLjExLS43LS4zek04IDcuOTljLS4wMi0uMjUtLjExLS40OC0uMzEtLjY5LS4yLS4xOS0uNDItLjMtLjY5LS4zMUg2Yy0uMjcuMDItLjQ4LjEzLS42OS4zMS0uMi4yLS4zLjQ0LS4zMS42OWgxdjNjLjAyLjI3LjExLjUuMzEuNjkuMi4yLjQyLjMxLjY5LjMxaDFjLjI3IDAgLjQ4LS4xMS42OS0uMzEuMi0uMTkuMy0uNDIuMzEtLjY5SDhWNy45OHYuMDF6TTcgMi4zYy0zLjE0IDAtNS43IDIuNTQtNS43IDUuNjggMCAzLjE0IDIuNTYgNS43IDUuNyA1LjdzNS43LTIuNTUgNS43LTUuN2MwLTMuMTUtMi41Ni01LjY5LTUuNy01LjY5di4wMXpNNyAuOThjMy44NiAwIDcgMy4xNCA3IDdzLTMuMTQgNy03IDctNy0zLjEyLTctNyAzLjE0LTcgNy03eiIgLz48L3N2Zz4=)note

The following guide uses **Windows** as an example, demonstrating development using **VS Code + the ESP-IDF extension**. macOS and Linux users should refer to the **[official documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)**.

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTYgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTguODkzIDEuNWMtLjE4My0uMzEtLjUyLS41LS44ODctLjVzLS43MDMuMTktLjg4Ni41TC4xMzggMTMuNDk5YS45OC45OCAwIDAgMCAwIDEuMDAxYy4xOTMuMzEuNTMuNTAxLjg4Ni41MDFoMTMuOTY0Yy4zNjcgMCAuNzA0LS4xOS44NzctLjVhMS4wMyAxLjAzIDAgMCAwIC4wMS0xLjAwMkw4Ljg5MyAxLjV6bS4xMzMgMTEuNDk3SDYuOTg3di0yLjAwM2gyLjAzOXYyLjAwM3ptMC0zLjAwNEg2Ljk4N1Y1Ljk4N2gyLjAzOXY0LjAwNnoiIC8+PC9zdmc+)Version Selection

The screenshots in this section use **ESP-IDF V5.5.2** as an example. When installing, please select the ESP-IDF version that matches your board's example.

### Install the ESP-IDF Development Environment

1.  Download the installation manager from the [ESP-IDF Installation Manager](https://dl.espressif.com/dl/eim/) page. This is Espressif's latest cross-platform installer. The following steps demonstrate how to use its offline installation feature.

    Click the **Offline Installer** tab on the page, then select **Windows** as the operating system and **the ESP-IDF version you need** (the version shown in the screenshot is for reference only — choose the version that fits your actual needs).

    ![Download EIM and offline package](images/01-EIM-Offline-Installation-1-4bf0df8afe2b21c6be95828003f0b94d.webp)

    After confirming your selection, click the download button. The browser will automatically download two files: the **ESP-IDF Offline Package (.zst)** and the **ESP-IDF Installer (.exe)**.

    ![Download EIM and offline package 2](images/01-EIM-Offline-Installation-2-6209ce57a020782adf348ae124ff6f67.webp)

    Please wait for both files to finish downloading.

2.  Once the download is complete, double-click to run the **ESP-IDF Installer (eim-gui-windows-x64.exe)**.

    The installer will automatically detect if the offline package exists in the same directory. Click **Install from archive**.

    ![Auto-detect offline package](images/01-EIM-Offline-Installation-4-e49c31342be5c78da1adfd7d9249c491.webp)

    Next, select the installation path. We recommend using the default path. If you need to customize it, ensure the path does not contain Chinese characters or spaces. Click **Start installation** to proceed.

    ![Select installation path](images/01-EIM-Offline-Installation-5-dc945b666c83940cf17180c98ed687d1.webp)

3.  When you see the following screen, the ESP-IDF installation is successful.

    ![Installation successful](images/01-EIM-Offline-Installation-6-1df9ba6d5d954dcbf8ff0bc231f19b64.webp)

4.  We recommend installing the drivers as well. Click **Finish installation**, then select **Install driver**.

    ![Install drivers via ESP-IDF Installation Manager](images/01-EIM-Offline-Installation-7-d6839851019130942567b2e0e51ed339.webp)

### Install Visual Studio Code and the ESP-IDF Extension

1.  Download and install [Visual Studio Code](https://code.visualstudio.com/).

2.  During installation, it is recommended to check **Add "Open with Code" action to Windows Explorer file context menu** to facilitate opening project folders quickly.

3.  In VS Code, click the **Extensions** icon ![Extensions Icon](data:image/webp;base64,UklGRmwCAABXRUJQVlA4IGACAACQDwCdASohACEAAAAAJZwDOK8c4IVLzgNsBogH9P/nPWAfsB7AH7Felh+xXwS/sd+4fs3//+6X4oPpf8t/IDwAN5n9QHoAapf+OXiV8TXrAH8r/3fn0/t35Af4D2p/LX+q/mHsZf5D8reMR/Tc7NJa7BmtoefSUDTIRXP24s6CSp4wehu4AP7//1fNCFT/kO3+oALgIvZh/Qe48AXTGbuqLluKdXKPacd9/pZl6Sty8X2HXk3b0py+5NjxmXyV02tHyuIUWdHyy/pRUTpke+tXvBFfoJ/53lx1/6fMiThKVy/z/I7PoY7XL3PuvFvQ1fQLrRkDgRf0rO++1UerDL4amvR7a4PBiSTax/TPRi7hzdRkf70rKea+iBPxTwMQJT/4KSrXf45KEfj3Uxk+qKOhJBzmIBEHZpkVHNc6ACZKxUrf/iyW5rkkYW3mMThmug+7MxU2eo4+srR27Polr235vgXoE6afM6dYRXiGiAEZQNdBWxmn7Tz0D35CMSySUld6zOcbdOXhMqZL5J3+U3r/e9jN7wyAtrQc/7KYt/2t0SX+PoorQwf48smvo4roQvsZjNg6OH/mA3/3n5ZXPeSrxmKFXv2fGW+fdf5NHYBu0o8V5aRRMcAsnyDJz6BP/yQXd0MXZxRP4gf9h9srGtTQrT6fysoN7+Q4WJT4PzGovPTT1tLVIHzj7STMmI6ftMo1SF6Mf/efmq/bgXqDNwQVMU58JOi6IoQ316N2Pa/OqFJ7M/bfXkk+DML+z87mbG+gw7n9AY1n4zkkHcH9V5d3+auNoeJ8TeJnQ6awAAAAAA==) in the Activity Bar on the side (or use the shortcut Ctrl + Shift + X) to open the **Extensions** view.

4.  Enter **ESP-IDF** in the search box, locate the [ESP-IDF](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) extension, and click Install.

    ![Search and install ESP-IDF extension in VS Code](images/01-VSCode-Install-ESP-IDF-Extension-7eb3e6eafde6406a3db6740ee801f182.webp)

5.  For **ESP-IDF extension versions ≥ 2.0**, the extension will automatically detect and recognize the ESP-IDF environment installed in the previous steps, requiring no manual configuration.

## Example

The ESP-IDF examples are located in the `examples/esp-idf` directory of the [ESP32-S3-Touch-AMOLED-1.75C Example Repository](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/tree/main/examples/esp-idf).

| Demo | Basic Description |
|----|----|
| [01_AXP2101](#Demo-1-AXP2101) | Gets power-related data by driving the AXP2101 via the ported XPowersLib |
| [02_lvgl_demo_v9](#Demo-2-lvgl_demo_v9) | LVGL Demonstration |
| [03_esp-brookesia](#Demo-3-esp-brookesia) | Demonstrates a complete phone-style UI system, including status bar, navigation bar, app launcher, and gesture interaction components |
| [04_Immersive_block](#Demo-4-Immersive_block) | Uses the QMI8658 six-axis sensor to collect acceleration data, driving randomly generated geometric shapes rendered by the LVGL graphics library to move in sync with device tilt |
| [05_Spec_Analyzer](#Demo-5-Spec_Analyzer) | Showcases a real-time audio spectrum visualization analyzer, presenting audio frequency distribution intuitively via 64 colored symmetrical spectrum bars with peak tracking |

### 01_AXP2101

#### Example Description

- This demo demonstrates porting XPowersLib in ESP-IDF, and driving AXP2101 to obtain power-related data through the ported XPowersLib

#### Hardware Connection

- Connect the development board to the computer

#### Code Analysis

- `i2c_init`: Initializes the I2C master device, preparing it for communication with other devices (e.g., the PMU)
  - Configures I2C parameters, including setting the master device mode, specifying the SDA and SCL pins, enabling the pull-up resistor, and determining the clock frequency
  - Installs the I2C driver to apply the configuration to the actual hardware
- `pmu_register_read`: Reads a series of byte data from a specific register of the PMU
  - Performs parameter checks to ensure the incoming parameters are valid and avoid invalid read operations
  - Performs I2C operations in two steps, first sends the register address to read, then reads the data During the reading process, different processing is carried out according to the length of bytes to be read to ensure accurate reading of the data. At the same time, handles error cases in the I2C communication process and returns the corresponding status code so that the upper-layer code can determine if the read operation is successful

#### Operation Result

- This demo will not light up the screen
- The serial port monitor displays the parameters: chip temperature, charging state, discharging state, standby state, Vbus connection, Vbus condition, charger status, battery voltage, Vbus voltage, system voltage, battery percentage

![idf_demo_AXP2101](images/idf_demo_AXP2101-432442444ff4b7ad1eeaecc74d7412e5.webp)

### 02_lvgl_demo_v9

#### Example Description

- This demo runs the LVGL V9 demo program

#### Hardware Connection

- Connect the development board to the computer

#### Operation Result

[TABLE]

### 03_esp-brookesia

#### Example Description

- This example demonstrates a complete phone-style UI system, including components such as a status bar, navigation bar, app launcher, and gesture interaction

#### Hardware Connection

- Connect the development board to the computer

#### Operation Result

[TABLE]

### 04_Immersive_block

#### Example Description

- This example uses the QMI8658 six-axis sensor to collect acceleration data, driving randomly generated geometric shapes rendered by the LVGL graphics library to move in sync with device tilt

#### Hardware Connection

- Connect the development board to the computer

#### Code Analysis

- `generate_random_shapes()`: Generates graphics randomly
  - Initializes the random seed (based on system time) and resets the shape count
  - Loops to attempt generating shapes, up to 100 attempts to find a valid position (not overlapping with existing shapes)
  - Valid position criteria: The shape's center is within the screen boundaries and does not overlap with any previously generated shape
  - For a valid position: Updates the LVGL object position (`lv_obj_set_pos`) and stores it in the shapes array
  - For an invalid position: Deletes the LVGL shape object to avoid memory leaks
- `perform_level_calibration()`: Core function for horizontal calibration
  - Collects 200 acceleration samples, calculates the average value for X/Y axes as the bias values (`accel_bias_x/y`)
  - Detects sample fluctuation range (if the range for X/Y axes is greater than 0.1, calibration is considered unstable and a retry is triggered)
  - After calibration is complete, sets the `calibration_done` flag and prints the bias information

#### Operation Result

![idf_demo_Immersive_block](data:image/webp;base64,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)

### 05_Spec_Analyzer

#### Example Description

- This example showcases a real-time audio spectrum visualization analyzer. It intuitively presents audio frequency distribution via 64 colored symmetrical spectrum bars with peak tracking

#### Hardware Connection

- Connect the development board to the computer

#### Code Analysis

- `timer_cb`: LVGL timer callback function, responsible for spectrum visualization rendering
  - Canvas and Layer Initialization: Gets the canvas object, initializes an LVGL layer (layer rendering improves refresh efficiency and prevents flickering), and clears the screen (black background)
  - Basic Parameter Calculation: Calculates spectrum bar width (canvas width / 64), canvas center Y coordinate (for symmetrical drawing), and spectrum bar gap
  - Iterates to render 64 spectrum bars
  - Layer Rendering Completion: Calls `lv_canvas_finish_layer` to commit the layer rendering and updates the canvas display
- `lv_example_canvas_10`: Canvas initialization
  - Static Draw Buffer Definition: Uses `LV_DRAW_BUF_DEFINE_STATIC` to define a 300×150 pixel RGB565 format draw buffer
  - Canvas Creation: Creates an LVGL canvas object, sets its size and centers it, and binds the static draw buffer
  - Timer Creation: Creates a 33ms timer, binds the `timer_cb` callback function, and passes the canvas object as user data to enable timed rendering

#### Expected Result

![idf_demo_Spec_Analyzer](images/idf_demo_Spec_Analyzer-bbd27e1b41ebebff7162537efaa96fe7.webp)

[![](data:image/svg+xml;base64,PHN2ZyBjbGFzcz0iZmVlZGJhY2stYnV0dG9uX19pY29uIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld2JveD0iMCAwIDI0IDI0IiBmaWxsPSJub25lIiBzdHJva2U9ImN1cnJlbnRDb2xvciIgc3Ryb2tlLXdpZHRoPSIyIiBzdHJva2UtbGluZWNhcD0icm91bmQiIHN0cm9rZS1saW5lam9pbj0icm91bmQiIGFyaWEtaGlkZGVuPSJ0cnVlIj48cGF0aCBkPSJNMjEgMTVhMiAyIDAgMCAxLTIgMkg3bC00IDRWNWEyIDIgMCAwIDEgMi0yaDE0YTIgMiAwIDAgMSAyIDJ6IiAvPjwvc3ZnPg==)Give Feedback](https://docs.google.com/forms/d/e/1FAIpQLSfayJEZ5J-dp-3Wq_dkWsVRhNuQ6C79_GNV62mYuFW8Mj6U8Q/viewform)
