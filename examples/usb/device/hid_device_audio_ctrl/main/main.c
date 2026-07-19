#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hid_audio_ctrl.h"
#include "iot_button.h"
#include "tusb.h"

static const char *TAG = "HID AUDIO CTRL";
static usb_phy_handle_t s_usb_phy_handle;
static button_handle_t s_boot_button;

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

static void boot_button_press_cb(void *button_handle, void *user_data)
{
    (void)button_handle;
    (void)user_data;
    ESP_LOGI(TAG, "Volume down report submitted: %s", hid_device_audio_ctrl() ? "yes" : "no");
}

static void boot_button_init(void)
{
    const button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 300,
        .gpio_button_config = {
            .gpio_num = CONFIG_HID_CONSUMER_BUTTON_GPIO,
            .active_level = 0,
        },
    };

    s_boot_button = iot_button_create(&button_config);
    if (s_boot_button == NULL) {
        ESP_LOGE(TAG, "Failed to create boot button");
        return;
    }
    ESP_ERROR_CHECK(iot_button_register_cb(s_boot_button, BUTTON_PRESS_DOWN, boot_button_press_cb, NULL));
    ESP_LOGI(TAG, "Volume-down button initialized on GPIO%d", CONFIG_HID_CONSUMER_BUTTON_GPIO);
}

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB device mounted");
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB device unmounted");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing USB HID Consumer Control device");

    usb_phy_init();
    if (!tusb_init()) {
        ESP_LOGE(TAG, "TinyUSB initialization failed");
        return;
    }

    BaseType_t task_created = xTaskCreate(tinyusb_device_task, "tinyusb_device", 4096, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TinyUSB device task");
        return;
    }

    boot_button_init();
}
