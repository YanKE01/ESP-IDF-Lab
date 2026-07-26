# HID Consumer Control Device

This example creates a USB HID Consumer Control device. Pressing the BOOT button
sends a volume-down key press followed by a release report. The button defaults
to GPIO0 on ESP32-S3 and GPIO35 on ESP32-P4, and can be changed in menuconfig.

Supported targets:

* ESP32-S3 using USB Full-Speed
* ESP32-P4 using USB High-Speed by default, with Full-Speed selectable in menuconfig

Dependencies:

* ESP-IDF 5.5
* `espressif/tinyusb` `0.21.0~1`
* `espressif/button` `3.x`
