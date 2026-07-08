#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "tusb.h"
#include "esp_private/usb_phy.h"
#include "usb_descriptors.h"

static const char *TAG = "OS_PROBE";

/* How long to wait after the host has read our configuration descriptor (i.e.
 * enumeration started) for a SET_CONFIGURATION (mount) to arrive. Linux/macOS
 * always configure the device within milliseconds; Windows refuses to configure
 * a driver-less vendor interface and leaves it at config 0 forever. So:
 *   mount within this window  -> Linux/macOS
 *   no mount within this window -> Windows
 * This signal is immune to the Windows 0xEE registry caching. */
#define CONFIG_WAIT_GRACE_MS   2000

typedef enum {
    HOST_UNKNOWN = 0,
    HOST_WINDOWS,
    HOST_LINUX_OR_OTHER,
} host_os_t;

static volatile bool s_mounted = false;

/* Bridges the verdict across the post-probe esp_restart(). RTC RAM survives a
 * software reset but is lost on power-off, so unplugging the (USB-powered) board
 * and moving it to another host naturally forces a fresh probe -- no stale,
 * cross-machine misdetection, and no flash wear. */
static RTC_NOINIT_ATTR uint32_t s_os_rtc;

static const char *os_str(host_os_t os)
{
    switch (os) {
    case HOST_WINDOWS:        return "WINDOWS";
    case HOST_LINUX_OR_OTHER: return "LINUX / macOS / other";
    default:                  return "UNKNOWN";
    }
}

/* Trust the cached verdict only after our own software reset; any other reset
 * reason (power-on, external/RST button, brownout, watchdog) forces a re-probe.
 * On a true power-on RTC RAM holds garbage, but the reset-reason gate rejects it
 * before the value is ever read, so no magic number is needed. */
static host_os_t rtc_read_os(void)
{
    if (esp_reset_reason() != ESP_RST_SW) {
        return HOST_UNKNOWN;
    }
    if (s_os_rtc != HOST_WINDOWS && s_os_rtc != HOST_LINUX_OR_OTHER) {
        return HOST_UNKNOWN;
    }
    return (host_os_t)s_os_rtc;
}

static void rtc_write_os(host_os_t os)
{
    s_os_rtc = (uint32_t)os;
}

static void usb_phy_init(void)
{
    usb_phy_handle_t phy_hdl;
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .target = USB_PHY_TARGET_INT,
#if CONFIG_TINYUSB_RHPORT_HS
        /* ESP32-P4: high-speed USB OTG 2.0; the driver routes this through the
         * UTMI PHY (the "Using UTMI PHY instead of requested internal PHY" log
         * is expected and harmless). */
        .otg_speed = USB_PHY_SPEED_HIGH,
#endif
    };
    usb_new_phy(&phy_conf, &phy_hdl);
}

static void tusb_device_task(void *arg)
{
    (void)arg;
    while (1) {
        tud_task();
    }
}

void tud_mount_cb(void)
{
    s_mounted = true;
    ESP_LOGI(TAG, "USB mounted (host issued SET_CONFIGURATION)");
}

void tud_umount_cb(void)
{
    s_mounted = false;
    ESP_LOGI(TAG, "USB unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
}

void tud_resume_cb(void)
{
}

static void report_result(host_os_t os)
{
    ESP_LOGI(TAG, "========================================");
    if (os == HOST_WINDOWS) {
        ESP_LOGI(TAG, " Host OS detected: WINDOWS");
        ESP_LOGI(TAG, " reason: enumerated but no SET_CONFIGURATION within %d ms", CONFIG_WAIT_GRACE_MS);
        ESP_LOGI(TAG, "         (Windows leaves a driver-less vendor device at config 0)");
    } else {
        ESP_LOGI(TAG, " Host OS detected: LINUX / macOS / other");
        ESP_LOGI(TAG, " reason: host issued SET_CONFIGURATION (device mounted)");
    }
    ESP_LOGI(TAG, "========================================");
}

static void os_probe_task(void *arg)
{
    (void)arg;
    bool have_config_tick = false;
    TickType_t config_tick = 0;
    host_os_t os = HOST_UNKNOWN;

    while (os == HOST_UNKNOWN) {
        vTaskDelay(pdMS_TO_TICKS(50));

        /* Linux/macOS: the host configured the device (SET_CONFIGURATION). */
        if (s_mounted) {
            os = HOST_LINUX_OR_OTHER;
            break;
        }

        /* Windows: it read our config descriptor (enumeration started) but, with
         * no driver for a bare vendor interface, never issues SET_CONFIGURATION.
         * If the grace window elapses with no mount, conclude Windows. */
        if (usb_descriptors_config_requested()) {
            if (!have_config_tick) {
                have_config_tick = true;
                config_tick = xTaskGetTickCount();
            } else if ((xTaskGetTickCount() - config_tick) > pdMS_TO_TICKS(CONFIG_WAIT_GRACE_MS)) {
                os = HOST_WINDOWS;
                break;
            }
        }
    }

    report_result(os);

    /* Stash the verdict in RTC RAM and reboot. After the reset (ESP_RST_SW) the
     * verdict is trusted from RTC, the probe is skipped, and a real Stage-2 build
     * would bring up the OS-specific USB stack (MSC for Windows, CDC for Linux)
     * from a clean state instead of this neutral probe. */
    rtc_write_os(os);
    ESP_LOGI(TAG, "Verdict saved to RTC RAM; restarting (software reset)...");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

void app_main(void)
{
    /* If our post-probe software reset left a verdict in RTC RAM, trust it and
     * skip re-probing. A power cycle (e.g. moving the board to another host)
     * clears RTC RAM and forces a fresh probe. */
    host_os_t cached = rtc_read_os();
    if (cached != HOST_UNKNOWN) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, " Host OS from RTC (software reset): %s", os_str(cached));
        ESP_LOGI(TAG, " probe skipped -- Stage-2 would start the");
        ESP_LOGI(TAG, " OS-specific USB stack from this cached verdict");
        ESP_LOGI(TAG, "========================================");
        return;
    }

    ESP_LOGI(TAG, "Fresh boot -- neutral USB probe, sniffing host OS via enumeration");

    usb_phy_init();
    if (!tusb_init()) {
        ESP_LOGE(TAG, "tusb_init failed");
        return;
    }

    xTaskCreate(tusb_device_task, "tusb", 4096, NULL, 5, NULL);
    xTaskCreate(os_probe_task, "os_probe", 3072, NULL, 4, NULL);
}
