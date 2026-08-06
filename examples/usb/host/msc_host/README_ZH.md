# CherryUSB MSC Host 示例

[English Version](README.md)

本示例在 ESP32-P4 或 ESP32-S3 上使用 CherryUSB 实现 USB Host MSC（Mass Storage Class），完成一次 U 盘文件系统测试：

1. 初始化 CherryUSB Host；
2. 等待 USB MSC 设备插入并完成枚举；
3. 将 FAT 文件系统挂载到 `/usb`；
4. 打印 U 盘根目录中的文件；
5. 创建并写入 `/usb/CHERRY.TXT`；
6. 重新打开该文件并读回内容。

应用不会额外创建任务，也没有常驻应用循环。`app_main` 使用任务通知等待 MSC 接口启动，然后执行一次文件测试。CherryUSB 自身仍会创建 Host Hub 和 MSC 初始化所需的内部任务。

## 支持的芯片

- ESP32-P4：使用 `sdkconfig.defaults.esp32p4`；
- ESP32-S3：使用 `sdkconfig.defaults.esp32s3`。

两个目标都使用 CherryUSB 的 ESP DWC2 Host Controller 驱动和 ESP-IDF 通用 USB PHY。

## 硬件准备

- 一块 ESP32-P4 或 ESP32-S3 开发板；
- 一个 FAT 格式的 USB U 盘，建议使用 FAT32；
- 能够工作在 USB Host 模式并向设备提供 5 V VBUS 的 USB 接口。

> 本示例会以写入模式打开 `/usb/CHERRY.TXT`。同名文件存在时会被覆盖，请不要使用存有重要数据的 U 盘进行首次测试。

## 依赖

CherryUSB 由 ESP-IDF Component Manager 根据 `main/idf_component.yml` 从 GitHub 拉取：

```yaml
dependencies:
  cherryusb:
    git: "https://github.com/cherry-embedded/CherryUSB.git"
    version: master
```

首次配置或构建时需要能够访问 GitHub。依赖下载后位于工程的 `managed_components/cherryusb` 目录。

## 编译与烧录

进入示例目录并加载 ESP-IDF 环境：

```bash
cd examples/usb/host/msc_host
. "$IDF_PATH/export.sh"
```

### ESP32-P4

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```

### ESP32-S3

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

将 `PORT` 替换为实际串口，例如 `/dev/ttyACM0` 或 `/dev/ttyUSB0`。

`idf.py set-target` 会加载公共的 `sdkconfig.defaults`，并继续加载对应的 `sdkconfig.defaults.<target>`。从其他芯片目标切换时应重新执行 `set-target`，不要直接复用旧的 `sdkconfig`。

## 运行过程

CherryUSB Host 安装完成后，插入 U 盘。正常日志流程如下：

```text
I (...) MSC_HOST: CherryUSB host installed
I (...) MSC_HOST: USB device connected: bus=0, hub=1, port=1
[I/usbh_core] Enumeration success, start loading class driver
[I/usbh_core] Loading msc class driver on interface 0
[I/usbh_msc] Register MSC Class:/dev/sda
I (...) MSC_HOST: MSC interface started, waiting for /usb
[I/usbh_msc] Capacity info:
I (...) MSC: MSC host filesystem mounted
I (...) MSC_HOST: MSC filesystem mounted at /usb
I (...) MSC_HOST: Files in /usb:
I (...) MSC_HOST: Created /usb/CHERRY.TXT
I (...) MSC_HOST: Read 32 bytes from /usb/CHERRY.TXT: Hello from CherryUSB MSC host!
```

U 盘拔出时会收到 `USBH_EVENT_INTERFACE_STOP` 和/或 `USBH_EVENT_DEVICE_DISCONNECTED`，应用会记录拔出事件。如果设备在文件系统挂载前被拔出，本次测试会直接结束。

## 初始化和挂载顺序

`USBH_EVENT_INTERFACE_START` 表示 MSC Class 已经注册为 `/dev/sda`，但此时 FAT 文件系统不一定已经挂载完成。CherryUSB 的 ESP-IDF FATFS 适配层还需要依次执行：

```text
MSC 接口启动
    -> SCSI 初始化及设备就绪检查
    -> 读取容量
    -> 注册 FatFS/VFS
    -> 挂载到 /usb
    -> 应用进行文件读写
```

因此 `run_msc_file_test()` 会每 100 ms 检查一次 `/usb`，最多等待约 10 秒；等待期间也会响应拔出通知。这是有限次数的挂载等待，不会重新执行 `usbh_initialize()`。

部分 U 盘刚插入时可能出现：

```text
[E/usbh_msc] csw bStatus 1
[W/usbh_msc] Device not ready, try again...
```

这是设备在上电后尚未准备好，CherryUSB 的 SCSI 初始化流程会重新执行 readiness 检查。随后能够打印容量并成功挂载时，可以视为正常现象。

## 关键配置

目标配置文件启用了以下功能：

```text
CONFIG_CHERRYUSB=y
CONFIG_CHERRYUSB_HOST=y
CONFIG_CHERRYUSB_HOST_DWC2_ESP=y
CONFIG_CHERRYUSB_HOST_MSC=y
CONFIG_FATFS_LFN_HEAP=y
```

`main/CMakeLists.txt` 还强制链接了 `msc_host_vfs_register`。这是为了拉入 CherryUSB 的 ESP-IDF FATFS 适配实现；否则链接器可能只保留 MSC Class 中的弱 `usbh_msc_run()`，导致设备能够枚举为 `/dev/sda`，但不会挂载到 `/usb`。

## 常见问题

### 已经出现 `/dev/sda`，为什么还没有 `/usb`？

`/dev/sda` 表示 MSC Class 枚举完成，`/usb` 则需要等待 SCSI 初始化和 FAT 挂载完成。示例会自动等待约 10 秒。

### 创建文件时报 `Invalid argument`

请确认已经看到 `MSC host filesystem mounted`，并使用适合 FatFS 的文件名。本示例固定使用短文件名 `CHERRY.TXT`，同时启用了长文件名堆内存支持。

### 只能读取，不能写入

检查 U 盘是否被写保护、文件系统是否损坏，以及供电是否稳定。建议先在电脑上将测试 U 盘格式化为 FAT32 并安全弹出，再重新测试。

### 修改配置后链接不到 `usbh_initialize` 或 `usbh_find_class_instance`

确认当前目标对应的 `sdkconfig.defaults.<target>` 中启用了 CherryUSB Host、DWC2 ESP Host 和 MSC，然后重新执行：

```bash
idf.py set-target esp32p4  # 或 esp32s3
idf.py reconfigure
idf.py build
```
