| 支持的芯片 | ESP32-P4 | ESP32-S31 |
| ---------- | -------- | --------- |

# USB Host 高速信号质量测试（TEST_PACKET）

[English Version](./README.md)

本示例将 USB-OTG 主机控制器切换到 USB-IF `TEST_PACKET` 测试模式，用于配合 USB 2.0 高速测试夹具和示波器测量眼图。

## 工作原理

USB 2.0 高速眼图由 DWC_OTG 主机端口发送的标准 `TEST_PACKET` 码型生成。示例通过设置 `HPRT.PrtTstCtl = 4'b0100` 进入该模式，具体流程如下：

1. 安装 USB Host Library，等待 USB 设备连接并完成枚举。
2. 检查已连接设备的设备类或接口类是否为 MSC，确保其为 U 盘。
3. U 盘枚举完成后等待 200 ms。
4. 将 DWC_OTG 主机端口切换到 `TEST_PACKET` 模式。
5. 关闭 DWC 全局中断，使拔出 U 盘并接入 USB 测试夹具后，控制器仍能持续发送测试包。

高速眼图测试需要支持 USB 2.0 High-Speed 的 USB-OTG 控制器，本示例支持 **ESP32-P4** 和 **ESP32-S31**。

## 硬件准备

- 一块带有 USB-OTG 接口的 ESP32-P4 或 ESP32-S31 开发板
- 一个支持 USB 2.0 High-Speed 的 U 盘，仅用于使主机控制器完成高速枚举
- USB 2.0 高速信号质量测试夹具（USB-IF 或厂商测试夹具）
- 带宽大于 2 GHz 的示波器及相应探头
- 独立的下载、日志或供电连接，确保更换 USB-OTG 端口上的设备时开发板不会断电或复位

USB 引脚和接口连接方式请参考 [USB 示例说明](../../README.md)，并以所用开发板的原理图为准。

## 编译和烧录

进入本示例目录：

```bash
cd examples/usb/host/signal_quality_test
```

设置目标芯片：

```bash
idf.py set-target esp32p4
# 或
idf.py set-target esp32s31
```

编译、烧录并打开串口监视器：

```bash
idf.py -p PORT flash monitor
```

请将 `PORT` 替换为开发板对应的串口设备名。按 `Ctrl-]` 退出串口监视器。

## 测试步骤

1. 烧录程序并打开串口监视器。
2. 将支持 High-Speed 的 U 盘接入开发板的 USB-OTG 主机端口。
3. 等待日志中出现 `TEST_PACKET operation successful`。
4. 保持开发板供电且不要复位，拔出 U 盘。
5. 将 USB 2.0 高速测试夹具接入同一 USB-OTG 端口。
6. 使用示波器捕获测试码型并测量眼图。

> 如果 U 盘只以 Full-Speed 枚举，程序会打印警告，但仍会尝试进入 `TEST_PACKET` 模式。要进行有效的 USB 2.0 高速眼图测试，请确保 U 盘、连接器、线缆和目标控制器均工作在 High-Speed 模式。

## 示例输出

```text
I (xxx) eye_diagram: USB 2.0 Host eye diagram (TEST_PACKET) example
I (xxx) eye_diagram: USB Host installed
I (xxx) eye_diagram: Client registered
I (xxx) eye_diagram: Waiting for USB flash drive to be connected
I (xxx) eye_diagram: Device address 1, High speed
*** Device descriptor ***
...
I (xxx) eye_diagram: MSC (U-disk) enumerated, wait 200 ms then enter TEST_PACKET
I (xxx) eye_diagram: HPRT.PrtTstCtl before = 0
I (xxx) eye_diagram: HPRT.PrtTstCtl after  = 4
I (xxx) eye_diagram: TEST_PACKET operation successful
I (xxx) eye_diagram: Unplug the U-disk and connect the USB test fixture / oscilloscope
I (xxx) eye_diagram: The host continues sending USB 2.0 test packets
```

## 注意事项

- 本示例用于 USB 2.0 High-Speed 电气信号质量测试，不是普通的 USB Host 功能示例。
- 必须先使用 MSC U 盘完成枚举；连接非 MSC 设备时，程序会关闭该设备并继续等待。
- 进入 `TEST_PACKET` 模式后，USB 控制器的全局中断会被关闭，USB Host 将不再按正常业务流程处理设备事件。
- 测试结束后，请复位或重新上电开发板，使 USB 控制器退出测试模式并恢复正常工作。
- 更换 U 盘和测试夹具时不要让开发板掉电，否则需要重新执行枚举和模式切换流程。
