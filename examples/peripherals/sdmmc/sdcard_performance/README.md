# SD Card Performance Test

This example measures the sequential read/write throughput of an SD card on the **ESP32-P4** or **ESP32-S31** using the SDMMC interface (slot 0, 4-bit, UHS-I SDR50 @ 100MHz).

The test mounts the card with FATFS, then writes a 16 MB file and reads it back using a 64 KB DMA-aligned buffer, printing the throughput in MB/s.

## Hardware

* **ESP32-P4:** ESP32-P4-Function-EV-Board, on-board Micro SD slot powered by the on-chip LDO VO4.
* **ESP32-S31:** slot 0 pin mapping below (also used by the ESP-IDF ESP32-S31 KORVO board configuration), with SDMMC UHS IO power supplied by on-chip LDO VO1. The host uses a 1.8 V IO supply for UHS-I; verify your board's power wiring before running.
* Use a UHS-I capable SD card. The default configuration enables PSRAM at 250MHz on both targets and requires compatible hardware.

The example uses ESP-IDF's default slot 0 IOMUX pin mapping for each target:

| Signal | ESP32-P4 GPIO | ESP32-S31 GPIO |
|:------:|:-------------:|:--------------:|
| CLK    | 43            | 24             |
| CMD    | 44            | 25             |
| D0     | 39            | 20             |
| D1     | 40            | 21             |
| D2     | 41            | 22             |
| D3     | 42            | 23             |

> ESP32-P4: To reach SDR50 reliably, the input sampling delay phase is fixed to `SDMMC_DELAY_PHASE_1`. With the default phase, reads at 100MHz may show intermittent CRC errors (0x109); if errors still appear, try `PHASE_2` / `PHASE_3`. ESP32-S31 uses the ESP-IDF default sampling configuration.

## How to Use

For ESP32-P4:

```bash
idf.py set-target esp32p4
idf.py build flash monitor
```

For ESP32-S31, activate an ESP-IDF checkout with `esp32s31` support (such as the ESP-IDF 6.1 development version), then run:

```bash
idf.py --preview set-target esp32s31
idf.py build flash monitor
```

The test runs once automatically in `app_main()` and prints the results.

## Example Output

Measured on the ESP32-P4-Function-EV-Board:

```
I (1405) sdcard_perf: Starting SD card performance test (16 MB)
I (2255) sdcard_perf: WRITE 16384 KB in 842 ms -> 19.91 MB/s
I (2935) sdcard_perf: READ  16384 KB in 685 ms -> 24.48 MB/s
I (2935) sdcard_perf: SD card performance test done
```

| Operation | Throughput |
|:---------:|:----------:|
| Write     | ~19.9 MB/s |
| Read      | ~24.5 MB/s |

> Actual throughput depends on the SD card grade/brand and the negotiated bus speed.
