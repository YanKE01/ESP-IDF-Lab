| Supported Targets | ESP32-P4 | ESP32-S31 |
| ----------------- | -------- | --------- |

# USB Host Eye Diagram Test (TEST_PACKET)

This example puts the USB-OTG host controller into USB-IF **TEST_PACKET** mode so that a High-Speed eye diagram can be measured on an oscilloscope.

## Overview

The USB 2.0 High-Speed eye diagram is generated from the standard TEST_PACKET pattern driven by the DWC_OTG host port (`HPRT.PrtTstCtl = 4'b0100`). The example:

1. Installs the USB Host Library and enumerates the attached device.
2. Checks whether the device is an MSC flash drive (U-disk).
3. Waits **200ms second** after a U-disk is enumerated.
4. Enters CTS TEST_PACKET mode (same HPRT programming as the CTS host electrical-test code in `hcd_dwc.c`).
5. Disables the DWC global interrupt so that unplugging the U-disk and attaching a USB test fixture does not stop the test packet stream./

### TEST_PACKET sequence (DWC_OTG)

1. Power on the core and load the DWC_OTG host driver.
2. Connect an HS U-disk and enumerate to High-Speed.
3. Program `HPRT.PrtTstCtl` to send test packets.
4. Remove the U-disk and connect the USB-IF / vendor test fixture (OPT). The host core continues sending test packets.
5. Capture the eye diagram on the oscilloscope.

High-Speed eye diagram measurement requires an HS-capable USB-OTG controller (**ESP32-P4**, **ESP32-S31**). 

## Hardware Required

* Development board with USB-OTG support
* A USB flash drive (used only to complete HS enumeration)
* USB 2.0 High-Speed test fixture and oscilloscope (> 2 GHz) for the actual eye diagram

Follow instructions in [examples/usb/README.md](../../README.md) for pin assignment.

## Build and Flash

```
idf.py -p PORT flash monitor
```

(Replace `PORT` with the serial port name.)

(To exit the serial monitor, type ``Ctrl-]``.)

## Example Output

```
I (xxx) eye_diagram: USB 2.0 Host eye diagram (TEST_PACKET) example
I (xxx) eye_diagram: USB Host installed
I (xxx) eye_diagram: Client registered
I (xxx) eye_diagram: Waiting for USB flash drive to be connected
I (xxx) eye_diagram: Device address 1, High speed
*** Device descriptor ***
...
I (xxx) eye_diagram: MSC (U-disk) enumerated, wait 200 ms then enter TEST_PACKET
I (xxx) eye_diagram: TEST_PACKET operation successful
I (xxx) eye_diagram: Unplug the U-disk and connect the USB test fixture / oscilloscope
I (xxx) eye_diagram: The host continues sending USB 2.0 test packets
```

After `TEST_PACKET operation successful`, keep the board powered, unplug the U-disk, and connect the test fixture to measure the eye diagram.
