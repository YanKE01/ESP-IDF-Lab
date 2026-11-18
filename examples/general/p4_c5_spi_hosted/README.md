# P4 + C5 ESP-Hosted over SPI

[English](README.md) | [简体中文](README_CN.md)

An ESP32-P4 host uses an ESP32-C5 as its Wi-Fi co-processor through [esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu), over **SPI full duplex**, and connects to an AP as a station.

The C5 firmware lives in `cp/` and is flashed through the C5's own USB/UART port. The P4 does not flash the C5.

## Flow

```
P4 boot
 └─ esp_wifi_init() starts ESP-Hosted
      ├─ pulse RESET (IO4) → C5 reboots from flash (SPI boot)
      ├─ C5 raises Handshake → SPI transport up
      └─ Wi-Fi STA connects, P4 gets an IP
```

## Hardware

- Host: ESP32-P4 (SPI2 master, pins routed through the GPIO matrix)
- Co-processor: ESP32-C5 (SPI2 slave)
- Two dev boards connected with jumper wires

### Pin Assignment

| Signal | ESP32-P4 | ESP32-C5 | Direction |
|---|---|---|---|
| CLK | IO8 | GPIO4 | P4 → C5 |
| MOSI | IO5 | GPIO10 | P4 → C5 |
| MISO | IO7 | GPIO5 | C5 → P4 |
| CS | IO6 | GPIO9 | P4 → C5 |
| Handshake | IO10 | GPIO23 | C5 → P4 |
| Data Ready | IO11 | GPIO24 | C5 → P4 |
| RESET | IO4 | EN / RST | P4 → C5 |
| GND | GND | GND | use 2~3 wires |

7 signals in total. P4 IO1 / IO2 / IO3 stay free.

The C5 pins are chosen for easy wiring and are routed through the GPIO matrix. None of them is a strapping pin. Leave C5 GPIO28 at its default high level (the dev board's BOOT button pulls it up) and do not wire anything to C5 GPIO27/28.

### Jumper Wire Notes

- Keep wires short (≤ 10 cm) and of similar length. Route a GND wire next to CLK.
- Power each board from its own USB and **share GND**.
- Neither side uses the SPI2 IO_MUX pins, so both go through the GPIO matrix. Start at 10 MHz. Raise `CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ` step by step, up to 40 MHz. Very high clocks may not work through the GPIO matrix.
- Make sure nothing on the C5 dev board (buttons, LEDs) loads GPIO4/5/9/10/23/24.

## Project Layout

```
p4_c5_spi_hosted/
├── main/              P4 host app: Wi-Fi STA over ESP-Hosted
├── cp/                C5 co-processor firmware (ESP-Hosted wifi/sta CP example, SPI)
├── partitions.csv
└── sdkconfig.defaults
```

| Component | Source |
|---|---|
| `espressif/esp_hosted` | Component Registry, `3.1.0` (keep host and `cp/` identical) |
| `espressif/esp_wifi_remote` | Component Registry, `>=1.3.1` |

## Configuration

Host (`sdkconfig.defaults`, `Component config → ESP-Hosted`):

| Option | Value |
|---|---|
| `CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE` | y (board presets lock the pins) |
| `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_SPI` | y |
| `CONFIG_ESP_HOSTED_HOST_SPI_MODE` | 3 |
| `CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ` | 10 |
| `CONFIG_ESP_HOSTED_HOST_SPI_CLK/MOSI/MISO/CS_GPIO` | 8 / 5 / 7 / 6 |
| `CONFIG_ESP_HOSTED_HOST_HANDSHAKE_GPIO` / `DATA_READY_GPIO` | 10 / 11 |
| `CONFIG_ESP_HOSTED_HOST_RESET_GPIO` | 4 |

Co-processor (`cp/sdkconfig.defaults`):

| Option | Value |
|---|---|
| `CONFIG_EH_TRANSPORT_CP_SPI` | y |
| `CONFIG_EH_TRANSPORT_CP_SPI_MODE_3` | y (must match the host) |
| `CONFIG_EH_TRANSPORT_CP_SPI_HSPI_GPIO_CLK/MOSI/MISO/CS` | 4 / 10 / 5 / 9 |
| `CONFIG_EH_TRANSPORT_CP_SPI_GPIO_HANDSHAKE` / `DATA_READY` | 23 / 24 |
| `CONFIG_EH_TRANSPORT_CP_SPI_GPIO_RESET` | -1 (host resets the C5 through EN) |

Project options (`Example Configuration`): `APP_WIFI_SSID`, `APP_WIFI_PASSWORD`, `APP_WIFI_MAX_RETRY`.

## Build

ESP-IDF v5.5 is required (ESP-Hosted 3.x needs `>=5.5`).

```sh
source /home/yanke/esp/esp-idf55/esp-idf/export.sh
```

### 1. C5 co-processor (connect the C5 board's USB)

```sh
idf.py -C cp -B cp/build set-target esp32c5
idf.py -C cp -B cp/build -p C5_PORT flash monitor
```

### 2. P4 host (connect the P4 board's USB)

```sh
idf.py set-target esp32p4
idf.py menuconfig    # set Wi-Fi SSID / password
idf.py -p P4_PORT flash monitor
```

## Troubleshooting

| Symptom | Check |
|---|---|
| Host waits forever for the transport | C5 runs the `cp/` firmware (C5 monitor log); RESET wired to EN; Handshake wiring (P4 IO10 ↔ C5 GPIO23) |
| Garbled data / checksum errors | Lower `CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ`; shorter wires; shared GND; SPI mode is the same on both sides |
| C5 does not boot after reset | C5 GPIO28 must be high at reset |

## References

- esp-hosted-mcu: `docs/getting-started-mcu.md`, `docs/design/spi-full-duplex.md`, `host/mcu/eh_host_mcu_transport/Kconfig.host.spi`, `coprocessor/eh_cp_transport/Kconfig.cp.spi`
