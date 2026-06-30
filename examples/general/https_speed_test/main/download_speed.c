#include "download_speed.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#define DOWNLOAD_BUF_SIZE (32 * 1024)
#define DOWNLOAD_CONNECTION_COUNT 1
#define START_DOWNLOAD_BIT BIT0
#define US_PER_MIB        (0.95367431640625)

#if CONFIG_SIMPLE_TEST_DOWNLOAD_BUFFER_IN_PSRAM
#define DOWNLOAD_BUFFER_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define DOWNLOAD_BUFFER_LOCATION "PSRAM"
#else
#define DOWNLOAD_BUFFER_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_CACHE_ALIGNED)
#define DOWNLOAD_BUFFER_LOCATION "internal RAM"
#endif

#if CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC
#define MBEDTLS_MEM_LOCATION "PSRAM"
#elif CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC
#define MBEDTLS_MEM_LOCATION "internal RAM"
#else
#define MBEDTLS_MEM_LOCATION "default allocator"
#endif

static const char *TAG = "HTTP_CLIENT";
static EventGroupHandle_t s_start_event;

typedef struct {
    int index;
} download_task_arg_t;

static void log_heap(int index, const char *phase)
{
    int dram_free = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024;
    int dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024;
    int dram_total = heap_caps_get_total_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024;
    int psram_free = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024;
    int psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024;
    int psram_total = heap_caps_get_total_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024;

    if (index >= 0) {
        ESP_LOGI(TAG,
                 "conn[%d] %s heap: DRAM used/free/largest/total: %d/%d/%d/%d KB, PSRAM used/free/largest/total: %d/%d/%d/%d KB",
                 index,
                 phase,
                 dram_total - dram_free, dram_free, dram_largest, dram_total,
                 psram_total - psram_free, psram_free, psram_largest, psram_total);
    } else {
        ESP_LOGI(TAG,
                 "%s heap: DRAM used/free/largest/total: %d/%d/%d/%d KB, PSRAM used/free/largest/total: %d/%d/%d/%d KB",
                 phase,
                 dram_total - dram_free, dram_free, dram_largest, dram_total,
                 psram_total - psram_free, psram_free, psram_largest, psram_total);
    }
}

static void download_task(void *arg)
{
    download_task_arg_t *task_arg = (download_task_arg_t *)arg;
    int index = task_arg->index;

    xEventGroupWaitBits(s_start_event, START_DOWNLOAD_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    uint8_t *buffer = heap_caps_malloc(DOWNLOAD_BUF_SIZE, DOWNLOAD_BUFFER_CAPS);
    assert(buffer);

    printf("conn[%d] test buffer size is %d KB, buffer: %s, task stack: internal RAM, mbedTLS: %s\n",
           index,
           DOWNLOAD_BUF_SIZE / 1024,
           DOWNLOAD_BUFFER_LOCATION,
           MBEDTLS_MEM_LOCATION);
    printf("conn[%d] download test url: %s\n", index, CONFIG_SIMPLE_TEST_DOWNLOAD_URL);
    log_heap(index, "after buffer alloc");

    esp_http_client_config_t config = {
        .url = CONFIG_SIMPLE_TEST_DOWNLOAD_URL,
        .buffer_size = DOWNLOAD_BUF_SIZE,
        .timeout_ms = 15 * 1000,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && !CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "conn[%d] init http client fail", index);
        goto cleanup;
    }
    log_heap(index, "after client init");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "conn[%d] open url fail[%d]", index, err);
        goto cleanup;
    }
    log_heap(index, "after tls open");

    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "conn[%d] fetch header fail[%" PRId64 "]", index, content_length);
        goto cleanup;
    }
    log_heap(index, "after fetch headers");

    ESP_LOGI(TAG, "conn[%d] file: %s, total size: %" PRId64,
             index, CONFIG_SIMPLE_TEST_DOWNLOAD_URL, content_length);

    int64_t received = 0;
    int64_t last_received = 0;
    int64_t total_start_time = esp_timer_get_time();
    int64_t start_time = total_start_time;
    int64_t read_duration = 0;
    int retry_count = 0;

    while (received < content_length) {
        int64_t read_start = esp_timer_get_time();
        int read_len = esp_http_client_read(client, (char *)buffer, DOWNLOAD_BUF_SIZE);
        read_duration += esp_timer_get_time() - read_start;

        if (read_len > 0) {
            received += read_len;
            retry_count = 0;
        } else if (read_len == 0 || read_len == -ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGI(TAG, "conn[%d] [%02d]download timeout, retry", index, retry_count);
            if (retry_count++ >= 3) {
                ESP_LOGE(TAG, "conn[%d] download fail", index);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } else {
            ESP_LOGE(TAG, "conn[%d] download fail[%d]", index, read_len);
            break;
        }

        int64_t cur_time = esp_timer_get_time();
        int64_t duration = cur_time - start_time;
        if (duration > 2 * 1000 * 1000) {
            int64_t temp_size = received - last_received;
            float speed = US_PER_MIB * temp_size / (duration * 1.0);
            int dram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024;
            int dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024;
            int dram_total = heap_caps_get_total_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024;
            int psram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024;
            int psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024;
            int psram_total = heap_caps_get_total_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024;
            ESP_LOGI(TAG, "conn[%d] received: %" PRId64 " KB(%d%%), speed: %.2f MB/s, DRAM used/free/largest/total: %d/%d/%d/%d KB, PSRAM used/free/largest/total: %d/%d/%d/%d KB",
                     index, received / 1024, (int)(100.0 * received / content_length), speed,
                     dram_total - dram, dram, dram_largest, dram_total,
                     psram_total - psram, psram, psram_largest, psram_total);
            start_time = cur_time;
            last_received = received;
        }
    }

    int64_t total_duration = esp_timer_get_time() - total_start_time;
    float http_speed = US_PER_MIB * received / (read_duration * 1.0);
    float total_speed = US_PER_MIB * received / (total_duration * 1.0);
    ESP_LOGI(TAG, "conn[%d] received: %" PRId64 " KB %" PRId64 " ms, http speed: %.2f MB/s, total speed: %.2f MB/s",
             index, received / 1024, total_duration / 1000, http_speed, total_speed);

cleanup:
    if (client != NULL) {
        esp_http_client_cleanup(client);
        log_heap(index, "after client cleanup");
    }
    free(buffer);
    vTaskDeleteWithCaps(NULL);
}

void download_speed_start(void)
{
    static download_task_arg_t task_args[DOWNLOAD_CONNECTION_COUNT];
    static char task_names[DOWNLOAD_CONNECTION_COUNT][16];

    s_start_event = xEventGroupCreate();
    assert(s_start_event);

    ESP_LOGI(TAG, "start %d parallel download connections", DOWNLOAD_CONNECTION_COUNT);
    log_heap(-1, "before creating download tasks");
    for (int i = 0; i < DOWNLOAD_CONNECTION_COUNT; i++) {
        task_args[i].index = i;
        snprintf(task_names[i], sizeof(task_names[i]), "download_%d", i);
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(download_task,
                         task_names[i],
                         4096,
                         &task_args[i],
                         5,
                         NULL,
                         0,
                         MALLOC_CAP_INTERNAL);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "create download task %d fail", i);
        }
    }
    log_heap(-1, "after creating download tasks");
    xEventGroupSetBits(s_start_event, START_DOWNLOAD_BIT);
}
