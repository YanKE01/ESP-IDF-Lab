#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mtp_storage.h"
#include "tusb.h"

static const char *TAG = "MTP";
static usb_phy_handle_t s_usb_phy_handle;

static void usb_phy_init(void)
{
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .otg_mode = USB_OTG_MODE_DEVICE,
#ifdef CONFIG_TINYUSB_RHPORT_HS
        .target = USB_PHY_TARGET_UTMI,
        .otg_speed = USB_PHY_SPEED_HIGH,
#else
        .target = USB_PHY_TARGET_INT,
        .otg_speed = USB_PHY_SPEED_FULL,
#endif
    };

    ESP_ERROR_CHECK(usb_new_phy(&phy_config, &s_usb_phy_handle));
}

static void tinyusb_device_task(void *arg)
{
    (void)arg;

    while (true) {
        tud_task();
    }
}

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB MTP device mounted");
}

void tud_umount_cb(void)
{
    mtp_storage_reset();
    ESP_LOGI(TAG, "USB MTP device unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    ESP_LOGI(TAG, "USB bus suspended");
}

void tud_resume_cb(void)
{
    ESP_LOGI(TAG, "USB bus resumed");
}

void app_main(void)
{
    ESP_ERROR_CHECK(mtp_storage_init());
    usb_phy_init();

    if (!tusb_init()) {
        ESP_LOGE(TAG, "TinyUSB initialization failed");
        return;
    }

    BaseType_t task_created = xTaskCreate(tinyusb_device_task, "tinyusb_device", 4096, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TinyUSB device task");
    }
}
