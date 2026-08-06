#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usbh_core.h"

#define MSC_MOUNT_POINT "/usb"
#define MSC_DEVNAME     "/dev/sda"
#define MSC_TEST_FILE   MSC_MOUNT_POINT "/CHERRY.TXT"

#define MSC_EVENT_STARTED      (1UL << 0)
#define MSC_EVENT_DISCONNECTED (1UL << 1)

static const char *TAG = "MSC_HOST";
static TaskHandle_t s_main_task;

static void list_files(void)
{
    DIR *dir = opendir(MSC_MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open %s: %s", MSC_MOUNT_POINT, strerror(errno));
        return;
    }

    ESP_LOGI(TAG, "Files in %s:", MSC_MOUNT_POINT);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }
    closedir(dir);
}

static void create_and_read_test_file(void)
{
    static const char test_data[] = "Hello from CherryUSB MSC host!\r\n";

    FILE *file = fopen(MSC_TEST_FILE, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to create %s: %s", MSC_TEST_FILE, strerror(errno));
        return;
    }

    size_t written = fwrite(test_data, 1, sizeof(test_data) - 1, file);
    if (written != sizeof(test_data) - 1) {
        ESP_LOGE(TAG, "Failed to write %s: wrote %u of %u bytes",
                 MSC_TEST_FILE, (unsigned)written, (unsigned)(sizeof(test_data) - 1));
        fclose(file);
        return;
    }

    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Failed to close %s after writing: %s",
                 MSC_TEST_FILE, strerror(errno));
        return;
    }
    ESP_LOGI(TAG, "Created %s", MSC_TEST_FILE);

    file = fopen(MSC_TEST_FILE, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to reopen %s: %s", MSC_TEST_FILE, strerror(errno));
        return;
    }

    char read_buffer[128];
    size_t read_size = fread(read_buffer, 1, sizeof(read_buffer) - 1, file);
    if (ferror(file)) {
        ESP_LOGE(TAG, "Failed to read %s", MSC_TEST_FILE);
        fclose(file);
        return;
    }
    read_buffer[read_size] = '\0';
    fclose(file);

    ESP_LOGI(TAG, "Read %u bytes from %s: %s",
             (unsigned)read_size, MSC_TEST_FILE, read_buffer);
}

static void run_msc_file_test(void)
{
    ESP_LOGI(TAG, "MSC interface started, waiting for %s", MSC_MOUNT_POINT);

    for (unsigned int retry = 0; retry < 100; retry++) {
        DIR *dir = opendir(MSC_MOUNT_POINT);
        if (dir != NULL) {
            closedir(dir);
            ESP_LOGI(TAG, "MSC filesystem mounted at %s", MSC_MOUNT_POINT);
            list_files();
            create_and_read_test_file();
            return;
        }

        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(100));
        if (events & MSC_EVENT_DISCONNECTED) {
            ESP_LOGW(TAG, "USB device disconnected before filesystem mount");
            return;
        }
    }

    ESP_LOGE(TAG, "Timed out waiting for %s", MSC_MOUNT_POINT);
}

static void usb_host_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
                                   uint8_t intf, uint8_t event)
{
    (void)intf;

    switch (event) {
    case USBH_EVENT_DEVICE_CONNECTED:
        ESP_LOGI(TAG, "USB device connected: bus=%u, hub=%u, port=%u",
                 busid, hub_index, hub_port);
        break;
    case USBH_EVENT_INTERFACE_START:
        if (usbh_find_class_instance(MSC_DEVNAME) != NULL) {
            xTaskNotify(s_main_task, MSC_EVENT_STARTED, eSetBits);
        }
        break;
    case USBH_EVENT_INTERFACE_STOP:
        xTaskNotify(s_main_task, MSC_EVENT_DISCONNECTED, eSetBits);
        break;
    case USBH_EVENT_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "USB device disconnected: bus=%u, hub=%u, port=%u",
                 busid, hub_index, hub_port);
        xTaskNotify(s_main_task, MSC_EVENT_DISCONNECTED, eSetBits);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    /*
     * app_main already runs in ESP-IDF's main_task. Save its handle for USB
     * event notifications; no additional application task is created.
     */
    s_main_task = xTaskGetCurrentTaskHandle();

    int ret = usbh_initialize(0, ESP_USBH_BASE, usb_host_event_handler);
    if (ret < 0) {
        ESP_LOGE(TAG, "CherryUSB host install failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "CherryUSB host installed");

    uint32_t events = 0;
    xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);
    if (events & MSC_EVENT_STARTED) {
        run_msc_file_test();
    }
}
