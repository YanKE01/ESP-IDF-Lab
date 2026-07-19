# ESP-IDF Lab

[English](README.md) | [简体中文](README_CN.md)

`esp-idf-lab` 是一个基于 ESP-IDF 的实践项目集合，包含用于学习 ESP 系列芯片功能的实验和参考实现。本项目基于 ESP-IDF Release V5.5 构建。

## 项目能力

本仓库提供了一系列实用示例，展示如何使用 ESP32 系列芯片（尤其是 ESP32-S3）实现不同功能：

### 🧠 深度学习与人工智能（`dl`、`algorithm`）

- **图像分类：** 使用 TensorFlow Lite Micro 对图像进行分类，例如 CIFAR10 和 MNIST。
- **数据预测：** 使用 TFLite 完成回归和预测任务，例如波士顿房价和正弦波预测。
- **音频处理：** 从音频中提取 MFCC（梅尔频率倒谱系数）特征，并使用 ESP-SR 实现语音控制灯光。

### 🌐 网络与云 API（`http`）

- **大语言模型集成：** 通过 HTTP API 接入 Moonshot Kimi、阿里云通义千问、讯飞星火等大语言模型，并提供通用的 ESP GPT 示例。
- **百度云服务：** 提供获取 Access Token、图像分类、语音识别和文本转语音（TTS）等实用集成示例。

### 🔉 音频控制（`audio`）

- **I2S 音频播放：** 使用 MAX98357A 等 I2S 功放实现实时音频播放。
- **录音与存储：** 通过 I2S 麦克风（例如 MSM261S4030H0）录制音频，并将其保存到 SD 卡。

### 🔌 USB 协议栈（`usb`）

- **USB 设备模拟：** 模拟 HID 键盘、HID 音频控制器和原生 CDC ACM（虚拟串口）等设备。
- **USB 数据传输：** 实现自定义批量传输、非复合设备 WinUSB 和 WebUSB。
- **USB 主机：** 提供 USB 主机功能示例。

### 💡 硬件与外设（`peripherals`）

- **LCD 显示：** 驱动 LCD 屏幕，并解析、显示存储在 SPIFFS 中的 JPEG 图像。
- **PWM 与控制：** 生成互补 PWM 信号并计算 PWM 参数。
- **通信总线：** 驱动 CAN、I2C、SPI 等常用总线，并实现 GPIO 高速翻转（ESP32-S3 上最高可达 8 MHz）。
- **信号控制：** 使用 RMT 和 PCNT 外设。

### 📜 数据协议（`protocols`）

- **序列化：** 解析和处理 JSON、Protobuf（使用 nanopb）以及 Protobuf-C 数据。
- **工业协议：** 实现 Modbus 通信。

### 🖥️ UI 与图形（`ui`）

- **LVGL 集成：** 使用 LVGL 创建图形用户界面，包括 LCD 与摄像头结合的示例。
- **自定义 UI：** 提供 `wououi` 等自定义 UI 项目。

### ⚙️ 系统与内核（`pie`、`system`）

- **处理器指令扩展（PIE）：** 提供汇编指令、数学运算和内存操作等示例。
- **构建系统：** 展示同时构建两个目标芯片等高级 CMake 用法（`build_two_target`）。
