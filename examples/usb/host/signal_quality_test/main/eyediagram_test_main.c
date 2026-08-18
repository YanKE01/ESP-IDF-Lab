/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "hal/usb_dwc_ll.h"

static const char *TAG = "eye_diagram";

#define CLIENT_NUM_EVENT_MSG        5
#define EYE_DIAGRAM_DELAY_MS        200
#define USB_HOST_CTRL_PORT_ID       0   /* HS controller on ESP32-P4; the only DWC on other targets */

/*
 * HPRT.PrtTstCtl values, matching the CTS host electrical-test configuration
 * in hcd_dwc.c (usb_dwc_ll_hprt_set_test_ctl / USB_HOST_PORT_TEST_CTRL_*).
 */
#define USB_HOST_PORT_TEST_CTRL_PACKET          4   /* 4'b0100, High-Speed eye diagram */

static SemaphoreHandle_t s_new_dev_sem;
static uint8_t s_dev_addr;
static usb_host_client_handle_t s_client_hdl;
static volatile bool s_test_packet_running;

/**
 * @brief Check whether a configuration descriptor contains an MSC (U-disk) interface
 *
 * @param[in] config_desc Active configuration descriptor
 *
 * @return
 *     - true  At least one interface has class USB_CLASS_MASS_STORAGE
 *     - false No MSC interface found
 */
static bool config_has_msc_interface(const usb_config_desc_t *config_desc)
{
    int offset = 0;
    const usb_intf_desc_t *intf_desc = (const usb_intf_desc_t *)usb_parse_next_descriptor_of_type(
                                           (const usb_standard_desc_t *)config_desc,
                                           config_desc->wTotalLength,
                                           USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                           &offset);
    while (intf_desc != NULL) {
        if (intf_desc->bInterfaceClass == USB_CLASS_MASS_STORAGE) {
            return true;
        }
        intf_desc = (const usb_intf_desc_t *)usb_parse_next_descriptor_of_type(
                        (const usb_standard_desc_t *)intf_desc,
                        config_desc->wTotalLength,
                        USB_B_DESCRIPTOR_TYPE_INTERFACE,
                        &offset);
    }
    return false;
}

/**
 * @brief Enter USB-IF TEST_PACKET mode on the DWC host port
 *
 * Writes HPRT.PrtTstCtl = 4'b0100 (same as usb_dwc_hal_port_test_ctrl() in the
 * CTS HCD). After this, the controller continuously transmits USB 2.0 test
 * packets. Global interrupts are then disabled so that unplugging the U-disk
 * or attaching a test fixture does not stop the stream.
 */
static void enter_test_packet_mode(void)
{
    usb_dwc_dev_t *hw = USB_DWC_LL_GET_HW(USB_HOST_CTRL_PORT_ID);

    ESP_LOGI(TAG, "HPRT.PrtTstCtl before = %" PRIu32, usb_dwc_ll_hprt_get_test_ctl(hw));
    usb_dwc_ll_hprt_set_test_ctl(hw, USB_HOST_PORT_TEST_CTRL_PACKET);
    ESP_LOGI(TAG, "HPRT.PrtTstCtl after  = %" PRIu32, usb_dwc_ll_hprt_get_test_ctl(hw));

    usb_dwc_ll_gahbcfg_dis_global_intr(hw);
    s_test_packet_running = true;

    ESP_LOGI(TAG, "TEST_PACKET operation successful");
    ESP_LOGI(TAG, "Unplug the U-disk and connect the USB test fixture / oscilloscope");
    ESP_LOGI(TAG, "The host continues sending USB 2.0 test packets");
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        s_dev_addr = event_msg->new_dev.address;
        xSemaphoreGive(s_new_dev_sem);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (!s_test_packet_running) {
            ESP_LOGW(TAG, "Device gone");
        }
        break;
    default:
        break;
    }
}

static void usb_host_lib_task(void *arg)
{
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
        .peripheral_map = 0,    /* Default: HS peripheral on HS-capable chips */
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB Host installed");
    xTaskNotifyGive((TaskHandle_t)arg);

    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

static void class_driver_task(void *arg)
{
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &s_client_hdl));
    ESP_LOGI(TAG, "Client registered");
    xTaskNotifyGive((TaskHandle_t)arg);

    while (1) {
        usb_host_client_handle_events(s_client_hdl, portMAX_DELAY);
    }
}

/**
 * @brief Open the newly enumerated device, print descriptors, and start TEST_PACKET if it is an MSC U-disk
 *
 * @return
 *     - true  TEST_PACKET started
 *     - false Device is not an MSC U-disk (or open failed)
 */
static bool handle_new_device(uint8_t dev_addr)
{
    usb_device_handle_t dev_hdl = NULL;
    esp_err_t err = usb_host_device_open(s_client_hdl, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_device_open failed: %s", esp_err_to_name(err));
        return false;
    }

    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(dev_hdl, &dev_info));
    const char *speed_str[] = {"Low", "Full", "High"};
    ESP_LOGI(TAG, "Device address %u, %s speed", dev_addr, speed_str[dev_info.speed]);

    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);

    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);

    bool is_udisk = (dev_desc->bDeviceClass == USB_CLASS_MASS_STORAGE) ||
                    config_has_msc_interface(config_desc);
    if (!is_udisk) {
        ESP_LOGW(TAG, "Connected device is not an MSC U-disk, waiting for next device");
        ESP_ERROR_CHECK(usb_host_device_close(s_client_hdl, dev_hdl));
        return false;
    }

    ESP_LOGI(TAG, "MSC (U-disk) enumerated, wait %d ms then enter TEST_PACKET", EYE_DIAGRAM_DELAY_MS);
    if (dev_info.speed != USB_SPEED_HIGH) {
        ESP_LOGW(TAG, "Device is not High-Speed; HS eye diagram typically requires HS enumeration");
    }

    vTaskDelay(pdMS_TO_TICKS(EYE_DIAGRAM_DELAY_MS));
    enter_test_packet_mode();
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "USB 2.0 Host eye diagram (TEST_PACKET) example");

    s_new_dev_sem = xSemaphoreCreateBinary();
    assert(s_new_dev_sem);

    TaskHandle_t main_task = xTaskGetCurrentTaskHandle();
    BaseType_t created = xTaskCreate(usb_host_lib_task, "usb_host", 4096, (void *)main_task, 2, NULL);
    assert(created == pdTRUE);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    created = xTaskCreate(class_driver_task, "class", 4096, (void *)main_task, 3, NULL);
    assert(created == pdTRUE);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    while (!s_test_packet_running) {
        ESP_LOGI(TAG, "Waiting for USB flash drive to be connected");
        xSemaphoreTake(s_new_dev_sem, portMAX_DELAY);
        if (handle_new_device(s_dev_addr)) {
            break;
        }
    }

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
