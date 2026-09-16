# PP-OCRv6 on ESP32-S31

Run PP-OCRv6 on ESP32-S31 to detect and recognize text in an embedded image.
The P4 models `det_s8` and `rec_s8` in `main/models` are embedded in flash.
Detection and recognition load one stage at a time to reduce memory use.
Keep PSRAM XIP disabled; detection weights are read directly from flash.

## Requirements

- ESP-IDF 6.1 with `esp32s31` support.
- ESP32-S31 board. Default configuration: 16 MB flash and 250 MHz PSRAM.
- Place `esp-dl` alongside `esp_demos` to provide the inference runtime.

## Build and Flash

Activate the ESP-IDF environment, then run from the repository root:

```sh
cd examples/dl/ppocr
idf.py --preview set-target esp32s31
idf.py --preview build
idf.py --preview -p PORT flash monitor
```

Replace `PORT` with your serial port. The example prints recognition results and timing on startup.
To test another image, replace `main/pp_ocr_v6.jpg` and rebuild.
