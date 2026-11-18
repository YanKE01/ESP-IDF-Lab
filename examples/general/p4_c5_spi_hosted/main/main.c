/*
 * ESP32-P4 host: talk to an ESP32-C5 co-processor over SPI (full duplex)
 * with ESP-Hosted and connect to Wi-Fi as a station.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "p4_c5";

/* The first esp_wifi_* call brings up ESP-Hosted, which resets the C5 and
 * waits for the SPI transport to come up. */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int s_retry;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < CONFIG_APP_WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGI(TAG, "retry connecting to AP (%d)", s_retry);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = CONFIG_APP_WIFI_SSID,
            .password = CONFIG_APP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(CONFIG_APP_WIFI_PASSWORD) == 0) {
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to SSID: %s", CONFIG_APP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "failed to connect to SSID: %s", CONFIG_APP_WIFI_SSID);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI %d MHz mode %d  CLK=%d MOSI=%d MISO=%d CS=%d HS=%d DR=%d RST=%d",
             CONFIG_ESP_HOSTED_HOST_SPI_CLK_MHZ, CONFIG_ESP_HOSTED_HOST_SPI_MODE,
             CONFIG_ESP_HOSTED_HOST_SPI_CLK_GPIO, CONFIG_ESP_HOSTED_HOST_SPI_MOSI_GPIO,
             CONFIG_ESP_HOSTED_HOST_SPI_MISO_GPIO, CONFIG_ESP_HOSTED_HOST_SPI_CS_GPIO,
             CONFIG_ESP_HOSTED_HOST_HANDSHAKE_GPIO, CONFIG_ESP_HOSTED_HOST_DATA_READY_GPIO,
             CONFIG_ESP_HOSTED_HOST_RESET_GPIO);

    wifi_init_sta();
}
