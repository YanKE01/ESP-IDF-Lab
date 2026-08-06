# CherryUSB MSC Host Example

[中文版本](README_ZH.md)

This example uses CherryUSB to implement a USB Host MSC (Mass Storage Class) application on ESP32-P4 or ESP32-S3. It performs one filesystem test on an attached USB flash drive:

1. Initializes the CherryUSB Host stack.
2. Waits for a USB MSC device to be connected and enumerated.
3. Mounts its FAT filesystem at `/usb`.
4. Lists files in the root directory.
5. Creates and writes `/usb/CHERRY.TXT`.
6. Reopens the file and reads its contents back.

The application does not create an additional task or run a permanent application loop. `app_main` waits for the MSC interface-start event through a task notification and then runs the file test once. CherryUSB still creates its own internal Host Hub and MSC initialization tasks.

## Supported targets

- ESP32-P4, configured by `sdkconfig.defaults.esp32p4`.
- ESP32-S3, configured by `sdkconfig.defaults.esp32s3`.

Both targets use CherryUSB's ESP DWC2 Host Controller driver and the generic ESP-IDF USB PHY interface.

## Hardware required

- An ESP32-P4 or ESP32-S3 development board.
- A USB flash drive with a FAT filesystem; FAT32 is recommended.
- A USB port operating in Host mode and capable of supplying 5 V VBUS to the USB device.

> This example opens `/usb/CHERRY.TXT` for writing. An existing file with the same name will be overwritten. Do not use a flash drive containing important data for the first test.

## Dependency

The ESP-IDF Component Manager downloads CherryUSB from GitHub according to `main/idf_component.yml`:

```yaml
dependencies:
  cherryusb:
    git: "https://github.com/cherry-embedded/CherryUSB.git"
    version: master
```

The first configure or build therefore requires access to GitHub. The downloaded dependency is placed in the project's `managed_components/cherryusb` directory.

## Build and flash

Enter the example directory and load the ESP-IDF environment:

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

Replace `PORT` with the actual serial port, such as `/dev/ttyACM0` or `/dev/ttyUSB0`.

`idf.py set-target` loads the common `sdkconfig.defaults` followed by the matching `sdkconfig.defaults.<target>`. Run `set-target` again when switching from another chip target instead of reusing its old `sdkconfig`.

## Running the example

Insert a USB flash drive after the CherryUSB Host is installed. A successful run follows this sequence:

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

Removing the flash drive generates `USBH_EVENT_INTERFACE_STOP` and/or `USBH_EVENT_DEVICE_DISCONNECTED`. The application logs the removal event. If the device is removed before the filesystem is mounted, the current test ends immediately.

## Initialization and mount sequence

`USBH_EVENT_INTERFACE_START` means the MSC class has been registered as `/dev/sda`, but the FAT filesystem might not be mounted yet. CherryUSB's ESP-IDF FATFS adapter still needs to perform these steps:

```text
MSC interface starts
    -> SCSI initialization and device-ready checks
    -> Read capacity
    -> Register FatFS/VFS
    -> Mount at /usb
    -> Application file access
```

For this reason, `run_msc_file_test()` checks `/usb` every 100 ms for up to approximately 10 seconds. It also handles a disconnect notification while waiting. This is a bounded wait for the mount operation; it does not call `usbh_initialize()` again.

Some flash drives may initially report:

```text
[E/usbh_msc] csw bStatus 1
[W/usbh_msc] Device not ready, try again...
```

This means the medium was not ready immediately after power-up. CherryUSB's SCSI initialization retries its readiness check. If the capacity is subsequently printed and the filesystem mounts successfully, this message can be treated as normal startup behavior for that device.

## Important configuration

The target-specific defaults enable:

```text
CONFIG_CHERRYUSB=y
CONFIG_CHERRYUSB_HOST=y
CONFIG_CHERRYUSB_HOST_DWC2_ESP=y
CONFIG_CHERRYUSB_HOST_MSC=y
CONFIG_FATFS_LFN_HEAP=y
```

`main/CMakeLists.txt` also forces the linker to retain `msc_host_vfs_register`. This pulls in CherryUSB's ESP-IDF FATFS adapter. Without it, the weak, empty `usbh_msc_run()` in the MSC class can satisfy the linker, allowing the device to enumerate as `/dev/sda` without ever being mounted at `/usb`.

## Troubleshooting

### `/dev/sda` is present, but `/usb` is not

`/dev/sda` indicates that MSC enumeration has completed. `/usb` becomes available only after SCSI initialization and the FAT mount complete. The example waits for up to approximately 10 seconds.

### Creating the file reports `Invalid argument`

Make sure `MSC host filesystem mounted` has already been printed and use a filename accepted by FatFS. This example uses the short filename `CHERRY.TXT` and also enables heap-based long filename support.

### The drive can be read but not written

Check whether the drive is write-protected, whether its filesystem is damaged, and whether VBUS is stable. It is recommended to format the test drive as FAT32 on a computer and eject it safely before testing again.

### `usbh_initialize` or `usbh_find_class_instance` is undefined at link time

Verify that the current target's `sdkconfig.defaults.<target>` enables CherryUSB Host, the ESP DWC2 Host driver, and MSC, then run:

```bash
idf.py set-target esp32p4  # or esp32s3
idf.py reconfigure
idf.py build
```
