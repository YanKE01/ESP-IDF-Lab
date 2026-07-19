#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"
#include "device/usbd.h"
#include "esp_private/usb_phy.h"

static const char *TAG = "CDC_ACM";

static void usb_phy_init(void)
{
    usb_phy_handle_t phy_hdl;
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .target = USB_PHY_TARGET_INT,
#if CONFIG_TINYUSB_RHPORT_HS
        .otg_speed = USB_PHY_SPEED_HIGH,
#endif
    };
    usb_new_phy(&phy_conf, &phy_hdl);
}

static void tusb_device_task(void *arg)
{
    while (1) {
        tud_task(); // TinyUSB device task
    }
}

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB device mounted");
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB device unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    ESP_LOGI(TAG, "USB suspended");
}

void tud_resume_cb(void)
{
    ESP_LOGI(TAG, "USB resumed");
}

static void cdc_handle_rx(uint8_t itf)
{
    uint8_t buf[CFG_TUD_CDC_RX_BUFSIZE];

    while (tud_cdc_n_available(itf)) {
        uint32_t len = tud_cdc_n_read(itf, buf, sizeof(buf));
        ESP_LOGI(TAG, "Received %lu bytes", (unsigned long)len);
        ESP_LOG_BUFFER_HEXDUMP(TAG, buf, len, ESP_LOG_INFO);
        tud_cdc_n_write(itf, buf, len);
    }

    tud_cdc_n_write_flush(itf);
}

#if CONFIG_CDC_ACM_RX_MODE_POLLING
static void tud_cdc_task(void *arg)
{
    (void)arg;

    while (1) {
        cdc_handle_rx(0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
#endif

#if CONFIG_CDC_ACM_RX_MODE_CALLBACK
void tud_cdc_rx_cb(uint8_t itf)
{
    cdc_handle_rx(itf);
}
#endif

void app_main(void)
{
    usb_phy_init();
    if (!tusb_init()) {
        ESP_LOGE(TAG, "TinyUSB init failed");
        return;
    }
    xTaskCreate(tusb_device_task, "tusb_device_task", 4096, NULL, 5, NULL);
#if CONFIG_CDC_ACM_RX_MODE_POLLING
    xTaskCreate(tud_cdc_task, "tud_cdc_task", 5 * 1024, NULL, 5, NULL);
#endif
}
