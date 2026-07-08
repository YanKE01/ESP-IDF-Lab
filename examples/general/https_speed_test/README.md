# Simple Download Test

Minimal ESP32-P4 download speed test.

It connects through `protocol_examples_common`, downloads `CONFIG_SIMPLE_TEST_DOWNLOAD_URL`, stores data only in an internal RAM buffer, and prints throughput. There is no SD card, FATFS, or LittleFS path.

## Test Data

In an RF shield box, the HTTPS-only download path can reach 4.8 MB/s.

Build:

```sh
source /home/yanke/esp/esp-idf55/esp-idf/export.sh
idf.py set-target esp32p4 build
```

Run the local server from this project:

```sh
python3 tools/https_speed_server.py --size-mb 20 --advertise-ip 192.168.0.196
```
