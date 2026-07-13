[English](README.md) | [中文](README_CN.md)

| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# ESP32-S3 Earliest GPIO Signal Example

This example asserts a customer signal at the earliest user extension point supported by ESP-IDF, without waiting for `app_main()`:

```text
Power on
  -> First-stage ROM bootloader
  -> ROM loads the second-stage bootloader into RAM
  -> bootloader_before_init() asserts the GPIO       <-- Customer signal
  -> bootloader_init()
  -> Partition selection and application image loading
  -> System and FreeRTOS initialization
  -> The first operation in app_main() deasserts the GPIO  <-- Pulse ends
```

`bootloader_before_init()` is the first operation performed by the second-stage bootloader. At this point, the flash cache, BSS, and most hardware have not been initialized, so the hook only uses direct GPIO register operations and does not call the GPIO driver, print logs, or access data in flash.

By default, GPIO16 is driven high. Change the configuration to match the pin connections on the actual ESP32-S3 board.

## GPIO Configuration

Edit `sdkconfig.defaults` directly:

```ini
CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO=16
CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL=1
```

The settings can also be changed in the `ESP32-S3 early customer signal` menu using `idf.py menuconfig`.

Avoid the following pins when selecting the output GPIO:

- GPIO22 through GPIO25, which do not exist on ESP32-S3;
- Flash, PSRAM, and strapping pins;
- Pins used by the selected board for UART, USB/JTAG, or other peripherals.

The GPIO output latch is programmed before output is enabled to prevent an opposite-level glitch during enable. The first operation in `app_main()` writes directly to the GPIO latch and restores the output to the inactive level. The default configuration therefore produces a measurement pulse that is asserted high by the bootloader and deasserted low by `app_main()`.

## Startup Optimization Configuration

This example includes the following startup-time optimizations:

| Configuration | Setting |
| --- | --- |
| Flash | QIO at 80 MHz; capacity depends on the board |
| Bootloader and startup logs | WARN |
| Application image validation on power-on reset | Skipped |
| RTC slow-clock calibration | Disabled |
| PSRAM | Disabled |

These settings involve reliability and security tradeoffs, especially skipping image validation and disabling RTC calibration, and each setting should be evaluated before use in a product.

## Build and Flash

```bash
source /home/yanke/esp/esp-idf55/esp-idf/export.sh
cd /home/yanke/project/esp_demos/examples/peripherals/gpio/early_gpio

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

After the application starts, it prints output similar to the following:

```text
APP_MAIN_TIME_MS=...
EARLY_SIGNAL_GPIO=16
EARLY_SIGNAL_LEVEL=1
APP_MAIN_SIGNAL_LEVEL=0
RESET_REASON=1
POWER_ON_RESET=yes
```

`APP_MAIN_TIME_MS` still represents the time at which `app_main()` is reached, not the time at which the early GPIO signal is asserted.

## Measure the Actual GPIO Response Time

Use an oscilloscope or logic analyzer to measure both signals simultaneously:

- Channel 1: CHIP_PU/EN;
- Channel 2: the configured customer GPIO.

The interval from the CHIP_PU/EN rising edge to the GPIO reaching its active level is the response time from releasing the chip from reset to asserting the customer signal. The active pulse width is the interval from the earliest bootloader hook to the first operation in `app_main()`, while the interval from the CHIP_PU/EN rising edge to the GPIO returning inactive is the total time required to reach `app_main()`. Serial logs cannot measure these early edges accurately.

The second-stage bootloader must first be loaded from flash into RAM by the ROM, so software cannot execute before this stage. An earlier signal requires the pin's power-on pull state or external hardware and cannot be implemented by normal ESP-IDF application code.
