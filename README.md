# ESP-IDF Lab

[English](README.md) | [简体中文](README_CN.md)

`esp-idf-lab` is a collection of practical experiments and reference projects for learning ESP chip features with ESP-IDF. This project is built on ESP-IDF Release V5.5.

## Project Capabilities

This repository provides practical examples demonstrating how to achieve various capabilities using the ESP32 series chips (especially ESP32-S3):

### 🧠 Deep Learning & AI (`dl`, `algorithm`)
- **Image Classification:** Categorize images (e.g., CIFAR10, MNIST) using TensorFlow Lite Micro.
- **Data Prediction:** Perform regression and prediction tasks like Boston Housing pricing and Sine wave prediction using TFLite.
- **Audio Processing:** Extract MFCC (Mel-frequency cepstral coefficients) features from audio, and implement voice-activated lighting using the ESP-SR library.

### 🌐 Network & Cloud API (`http`)
- **LLM Integration:** Connect deeply with various Large Language Models via HTTP APIs, including Moonshot Kimi, Aliyun Tongyi Qianwen, Xunfei Xinghuo, and a general ESP GPT example.
- **Baidu Cloud Services:** Practical integrations for Baidu Access Token retrieval, Image Classification, Speech Recognition, and Text-to-Speech (TTS).

### 🔉 Audio Control (`audio`)
- **I2S Audio Playback:** Implement real-time audio playback using I2S amplifiers like MAX98357A.
- **Recording & Storage:** Record audio via I2S microphone (e.g., MSM261S4030H0) and save it directly to an SD Card.

### 🔌 USB Protocol Stack (`usb`)
- **USB Device Emulation:** Emulate devices like HID Keyboard, HID Audio Control, and native CDC ACM (Virtual COM Port).
- **USB Data Transfer:** Implement custom bulk transfers, WinUSB (no composite), and WebUSB.
- **USB Host:** Examples for USB Host features.

### 💡 Hardware & Peripherals (`peripherals`)
- **LCD Displays:** Drive LCD screens and parse JPEG images from SPIFFS to display them.
- **PWM & Control:** Generate complementary PWM signals and calculate PWM information.
- **Communication Buses:** Drive common protocols like CAN, I2C, SPI, and GPIO high-speed flipping (up to 8MHz on S3).
- **Signal Control:** Utilize RMT and PCNT.

### 📜 Data Protocols (`protocols`)
- **Serialization:** Parse and handle JSON data, Protobuf (using nanopb), and Protobuf-C.
- **Industrial Protocols:** Implement Modbus communication.

### 🖥️ UI & Graphics (`ui`)
- **LVGL Integration:** Create graphical user interfaces using LVGL, including examples combining LCD and camera.
- **Custom UI:** Projects like `wououi`.

### ⚙️ System & Core (`pie`, `system`)
- **Processor Instruction Extensions (PIE):** Examples covering assembly instructions, math, and memory operations on the core.
- **Build System:** Advanced CMake features like building for two targets (`build_two_target`).
