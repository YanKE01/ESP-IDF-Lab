[English](README.md) | [中文](README_CN.md)

| 支持的芯片 | ESP32-S3 |
| ---------- | -------- |

# ESP32-S3 最早 GPIO 正常信号示例

本例在 ESP-IDF 支持的最早用户扩展点输出客户正常信号，不等待 `app_main()`：

```text
上电
  -> 一级 ROM Bootloader
  -> ROM 将二级 Bootloader 装入 RAM
  -> bootloader_before_init() 拉起 GPIO       <-- 客户信号
  -> bootloader_init()
  -> 分区选择和应用镜像加载
  -> 系统和 FreeRTOS 初始化
  -> app_main() 第一项操作将 GPIO 恢复为无效电平  <-- 脉冲结束
```

`bootloader_before_init()` 是二级 Bootloader 的第一项操作。此时 flash cache、BSS 和大部分硬件尚未初始化，因此钩子只使用直接 GPIO 寄存器操作，不调用 GPIO 驱动、不打印日志，也不访问 flash 数据。

默认设置为 GPIO16 输出高电平。请根据实际 ESP32-S3 板卡的引脚连接修改配置。

## 配置 GPIO

直接编辑 `sdkconfig.defaults`：

```ini
CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO=16
CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL=1
```

也可以在 `idf.py menuconfig` 的 `ESP32-S3 early customer signal` 菜单中修改。

选择引脚时请避开：

- ESP32-S3 不存在的 GPIO22～GPIO25；
- Flash、PSRAM 和启动绑带引脚；
- 当前板卡正在使用的 UART、USB/JTAG 或其他外设引脚。

GPIO 输出锁存值会先设置，再启用输出，避免使能瞬间产生反向毛刺。进入 `app_main()` 后，第一项操作会直接写 GPIO 锁存寄存器，将输出恢复为相反电平。默认配置因此会形成“Bootloader 拉高、app_main 拉低”的测量脉冲。

## 启动优化配置

本例带有用于启动时间测试的优化：

| 配置 | 设置 |
| --- | --- |
| Flash | QIO、80 MHz；容量由具体板卡决定 |
| Bootloader 和启动日志 | WARN |
| 上电应用镜像校验 | 跳过 |
| RTC 慢时钟校准 | 关闭 |
| PSRAM | 关闭 |

这些配置有可靠性和安全性取舍，特别是跳过镜像校验和关闭 RTC 校准。用于产品前应分别评估。

## 构建和烧录

```bash
source /home/yanke/esp/esp-idf55/esp-idf/export.sh
cd /home/yanke/project/esp_demos/examples/peripherals/gpio/early_gpio

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

应用启动后会输出：

```text
APP_MAIN_TIME_MS=...
EARLY_SIGNAL_GPIO=16
EARLY_SIGNAL_LEVEL=1
APP_MAIN_SIGNAL_LEVEL=0
RESET_REASON=1
POWER_ON_RESET=yes
```

`APP_MAIN_TIME_MS` 仍然表示到达 `app_main()` 的时间，不是早期 GPIO 的输出时间。

## 测量真正的 GPIO 响应时间

使用示波器或逻辑分析仪同时测量：

- 通道 1：CHIP_PU/EN；
- 通道 2：配置的客户 GPIO。

从 CHIP_PU/EN 上升沿到客户 GPIO 到达有效电平的时间，就是从芯片上电释放到客户正常信号的响应时间。客户 GPIO 的有效脉冲宽度则是从最早 Bootloader hook 到 `app_main()` 第一项操作的时间；从 CHIP_PU/EN 上升沿到 GPIO 恢复无效电平，是到达 `app_main()` 的总时间。串口日志无法精确测量这些早期边沿。

二级 Bootloader 必须先由 ROM 从 flash 装入 RAM，所以软件不能早于这一阶段执行。如果还要提前，只能使用引脚的上电默认上下拉或外部硬件电路，不能通过普通 ESP-IDF 应用代码实现。
