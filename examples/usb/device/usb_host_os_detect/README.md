# Windows / Linux Host OS Detection

[中文版 / Chinese](./README_ZH.md)

This example shows how a USB device can tell whether the host is Windows or Linux/macOS without ever asking it, purely by observing how the host enumerates the device.

## Principle

The USB protocol has no "what OS are you" request, so the device can only infer the host OS from differences in enumeration behavior.

The key, cache-immune fingerprint is whether the host configures a driver-less vendor interface.

For a bare vendor device (interface class 0xFF, no driver, no WCID): Linux/macOS always issue `SET_CONFIGURATION` during enumeration even when no driver binds, while Windows refuses to configure it and leaves it at configuration 0.

So the device decides as follows: `SET_CONFIGURATION` arrives (mount) means Linux/macOS, while the configuration descriptor is read but no `SET_CONFIGURATION` arrives within a grace window means Windows.

The textbook Windows signal is the `GET_DESCRIPTOR(String, 0xEE)` "MSFT100" probe, but Windows caches its result in the registry (`...\Control\usbflags\<VID><PID><REV>`, value `osvc`) and never asks again on later replugs of the same VID/PID, so this example deliberately does not use it and relies solely on the cache-immune `SET_CONFIGURATION` fingerprint.

## Detection flow

```
power-on boot -> a neutral vendor interface enumerates
  - host reads our config descriptor (enumeration started)
      - SET_CONFIGURATION arrives (mount)        -> LINUX / macOS / other
      - no SET_CONFIGURATION within 2000 ms       -> WINDOWS
  - verdict -> save to RTC RAM -> esp_restart()
software-reset boot -> read verdict from RTC RAM, skip probe
```

## Persisting the verdict across a restart (RTC RAM + reset reason)

Real devices probe once, then reboot so the actual OS-specific USB stack comes up from a clean state, so the verdict has to survive that reboot.

This example carries the verdict in an `RTC_NOINIT_ATTR` variable, which survives a software reset but is cleared on power-off.

On boot the verdict is trusted only when `esp_reset_reason()` is `ESP_RST_SW` (our own post-probe `esp_restart()`); any other reset reason, including a true power-on where RTC RAM holds garbage, forces a fresh probe, so no magic-number validation is needed.

Choosing RTC RAM over NVS is deliberate: a USB-bus-powered board loses power when you unplug it and move it to a different host, which clears the verdict and re-probes automatically, avoiding stale cross-machine misdetection and incurring no flash wear.

On the software-reset boot this example just prints the cached verdict; a Stage-2 build would instead bring up MSC for Windows or CDC-ACM for Linux from that verdict.

## Distinguishing a Windows host from no host at all

A plain timeout cannot tell "Windows is present but refusing to configure us" apart from "nothing is plugged in", because both look like silence on the bus.

The trick is that the grace timer is not anchored to boot time, it is anchored to the moment the host fetches our configuration descriptor (`usb_descriptors_config_requested()` in the code).

A configuration descriptor is read only when a real host actually starts enumerating the device, so its arrival is positive proof that a host is present and talking to us.

If the board is unplugged, on a power-only cable, or wired to the UART bridge port instead of the native USB port, the configuration descriptor is never requested, the timer never starts, and the device keeps waiting silently instead of falsely reporting Windows.

Reading only the device descriptor does not arm the timer either, so a host that probes the device and immediately gives up will not trigger a false Windows verdict.

In short: Windows = host read our config descriptor AND then stayed silent for the grace window, whereas no host = config descriptor never read, so the two are never confused.

## Usage

Build and flash for a chip with native USB-OTG (esp32s3 / esp32s2 / esp32p4):

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

Connect the board's native USB port (GPIO19 D-, GPIO20 D+), not the UART bridge port, to the host under test.

The verdict is printed on the UART console.

## Notes and caveats

The Windows verdict is a timeout-based negative decision, so it has a fixed ~2 s latency; tune `CONFIG_WAIT_GRACE_MS`, but too short risks a false verdict on slow Windows hosts.

The probe must stay driver-less, so do not add a WCID / MS OS 2.0 descriptor; if you install a WinUSB/Zadig driver for this VID/PID, Windows will configure the device and be misdetected as Linux.

Embedded Linux, Android and the BSDs also configure the device, so they all fall under "LINUX / macOS / other".

After a verdict the device reboots once and then trusts the RTC-cached result, so to re-probe you must power-cycle the board (clearing RTC RAM), not just press RST or replug the data port.
