# USB MTP Device

This example exposes an SPIFFS partition to the host through USB Media Transfer Protocol.
Files can be listed, read, uploaded and deleted from Windows Explorer or another MTP client.
SPIFFS is a flat filesystem, so this example exposes only files in the storage root and does not support folders.
See [MTP interaction notes](MTP_NOTES.md) for command parameters and returned field definitions.

Supported targets:

* ESP32-S3 using USB Full-Speed
* ESP32-P4 using USB High-Speed by default, with Full-Speed selectable in menuconfig

The custom partition table creates a 1 MB SPIFFS partition named `storage`.
When the filesystem is empty, the application creates `readme.txt`.
Formatting on mount failure and the maximum number of exposed files can be configured under `USB MTP Configuration` in menuconfig.

Dependencies:

* ESP-IDF 5.5
* `espressif/tinyusb` `0.21.0~1`

Build for ESP32-S3:

```bash
idf.py set-target esp32s3
idf.py build
```

Build for ESP32-P4:

```bash
idf.py set-target esp32p4
idf.py build
```
