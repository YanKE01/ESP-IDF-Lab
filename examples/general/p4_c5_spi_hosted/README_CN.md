# P4 + C5 ESP-Hosted（SPI）

[English](README.md) | [简体中文](README_CN.md)

ESP32-P4 作为主机，通过 [esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu) 以 **SPI 全双工** 使用 ESP32-C5 作为 Wi-Fi 协处理器，并作为 STA 连接 AP。

C5 固件位于 `cp/`，通过 C5 开发板自己的 USB/UART 烧录，P4 不负责烧录 C5。

## 工作流程

```
P4 上电
 └─ esp_wifi_init() 启动 ESP-Hosted
      ├─ RESET（IO4）脉冲 → C5 从 flash 启动（SPI Boot）
      ├─ C5 拉起 Handshake → SPI 链路建立
      └─ Wi-Fi STA 连接，P4 获取 IP
```

## 硬件

- Host：ESP32-P4（SPI2 主机，引脚经 GPIO 矩阵）
- Co-processor：ESP32-C5（SPI2 从机）
- 两块开发板杜邦线连接

### 引脚分配

| 信号 | ESP32-P4 | ESP32-C5 | 方向 |
|---|---|---|---|
| CLK | IO8 | GPIO4 | P4 → C5 |
| MOSI | IO5 | GPIO10 | P4 → C5 |
| MISO | IO7 | GPIO5 | C5 → P4 |
| CS | IO6 | GPIO9 | P4 → C5 |
| Handshake | IO10 | GPIO23 | C5 → P4 |
| Data Ready | IO11 | GPIO24 | C5 → P4 |
| RESET | IO4 | EN / RST | P4 → C5 |
| GND | GND | GND | 2~3 根 |

共 7 根信号线，P4 的 IO1 / IO2 / IO3 空闲。

C5 引脚按走线方便选取，经 GPIO 矩阵连接，均不是 strapping 脚。C5 GPIO28 保持默认高电平即可（开发板 BOOT 按键已上拉），C5 GPIO27/28 不要接线。

### 杜邦线注意事项

- 线尽量短（≤ 10 cm）且长度接近，CLK 旁边走一根 GND。
- 两板各自 USB 供电，**务必共地**。
- 两端都没用 SPI2 的 IO_MUX 引脚，都经 GPIO 矩阵。从 10 MHz 起调，逐步提高 `CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ`，最高 40 MHz。走 GPIO 矩阵时过高频率可能不稳定。
- 确认 C5 开发板上 GPIO4/5/9/10/23/24 没有接按键、LED 等负载。

## 工程结构

```
p4_c5_spi_hosted/
├── main/              P4 主机应用：通过 ESP-Hosted 连接 Wi-Fi
├── cp/                C5 协处理器固件（ESP-Hosted wifi/sta CP 示例，SPI）
├── partitions.csv
└── sdkconfig.defaults
```

| 组件 | 来源 |
|---|---|
| `espressif/esp_hosted` | 组件仓库 `3.1.0`（host 与 `cp/` 必须一致） |
| `espressif/esp_wifi_remote` | 组件仓库 `>=1.3.1` |

## 配置

主机（`sdkconfig.defaults`，`Component config → ESP-Hosted`）：

| 选项 | 值 |
|---|---|
| `CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE` | y（官方板型预设会锁定引脚） |
| `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_SPI` | y |
| `CONFIG_ESP_HOSTED_HOST_SPI_MODE` | 3 |
| `CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ` | 10 |
| `CONFIG_ESP_HOSTED_HOST_SPI_CLK/MOSI/MISO/CS_GPIO` | 8 / 5 / 7 / 6 |
| `CONFIG_ESP_HOSTED_HOST_HANDSHAKE_GPIO` / `DATA_READY_GPIO` | 10 / 11 |
| `CONFIG_ESP_HOSTED_HOST_RESET_GPIO` | 4 |

协处理器（`cp/sdkconfig.defaults`）：

| 选项 | 值 |
|---|---|
| `CONFIG_EH_TRANSPORT_CP_SPI` | y |
| `CONFIG_EH_TRANSPORT_CP_SPI_MODE_3` | y（必须与主机一致） |
| `CONFIG_EH_TRANSPORT_CP_SPI_HSPI_GPIO_CLK/MOSI/MISO/CS` | 4 / 10 / 5 / 9 |
| `CONFIG_EH_TRANSPORT_CP_SPI_GPIO_HANDSHAKE` / `DATA_READY` | 23 / 24 |
| `CONFIG_EH_TRANSPORT_CP_SPI_GPIO_RESET` | -1（由主机通过 EN 复位 C5） |

工程选项（`Example Configuration`）：`APP_WIFI_SSID`、`APP_WIFI_PASSWORD`、`APP_WIFI_MAX_RETRY`。

## 构建

需要 ESP-IDF v5.5（ESP-Hosted 3.x 要求 `>=5.5`）。

```sh
source /home/yanke/esp/esp-idf55/esp-idf/export.sh
```

### 1. C5 协处理器（连接 C5 开发板 USB）

```sh
idf.py -C cp -B cp/build set-target esp32c5
idf.py -C cp -B cp/build -p C5_PORT flash monitor
```

### 2. P4 主机（连接 P4 开发板 USB）

```sh
idf.py set-target esp32p4
idf.py menuconfig    # 设置 Wi-Fi SSID / 密码
idf.py -p P4_PORT flash monitor
```

## 问题排查

| 现象 | 检查 |
|---|---|
| 主机一直等待链路建立 | C5 是否运行 `cp/` 固件（看 C5 串口日志）；RESET 是否接 EN；Handshake 接线（P4 IO10 ↔ C5 GPIO23） |
| 数据错乱 / 校验错误 | 降低 `CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ`；缩短线；共地；两端 SPI mode 一致 |
| 复位后 C5 不启动 | 复位时 C5 GPIO28 必须为高 |

## 参考

- esp-hosted-mcu：`docs/getting-started-mcu.md`、`docs/design/spi-full-duplex.md`、`host/mcu/eh_host_mcu_transport/Kconfig.host.spi`、`coprocessor/eh_cp_transport/Kconfig.cp.spi`
