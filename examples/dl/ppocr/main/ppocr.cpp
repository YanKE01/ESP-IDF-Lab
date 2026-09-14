#include "dl_image_jpeg.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pp_ocr_v6.hpp"

static const char *TAG = "ppocr";

extern const uint8_t pp_ocr_v6_jpg_start[] asm("_binary_pp_ocr_v6_jpg_start");
extern const uint8_t pp_ocr_v6_jpg_end[] asm("_binary_pp_ocr_v6_jpg_end");

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "%s: internal free=%u, PSRAM free=%u, largest SIMD block=%u", stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD)));
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PP-OCRv6 on %s, using main/models from flash rodata", CONFIG_IDF_TARGET);
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        ESP_LOGE(TAG, "PSRAM is required to load and run PP-OCRv6");
        return;
    }
    log_memory("Before decoding");

    {
        dl::image::jpeg_img_t jpeg_img = {
            .data = const_cast<uint8_t *>(pp_ocr_v6_jpg_start),
            .data_len = static_cast<size_t>(pp_ocr_v6_jpg_end - pp_ocr_v6_jpg_start),
        };
        auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
        if (!img.data) {
            ESP_LOGE(TAG, "Failed to decode embedded JPEG");
            return;
        }
        log_memory("After decoding");

        pp_ocr_v6::PPOCRV6 ocr;
        ESP_LOGI(TAG, "Running OCR on %ux%u image", static_cast<unsigned>(img.width),
                 static_cast<unsigned>(img.height));
        const int64_t run_start = esp_timer_get_time();
        const auto results = ocr.run(img);
        const int64_t run_ms = (esp_timer_get_time() - run_start) / 1000;
        heap_caps_free(img.data);
        if (ocr.get_last_error() != ESP_OK) {
            ESP_LOGE(TAG, "OCR failed: %s", esp_err_to_name(ocr.get_last_error()));
            log_memory("After cleanup");
            return;
        }

        for (const auto &res : results) {
            ESP_LOGI(TAG, "text=\"%s\", score=%.4f, box=[%d,%d %d,%d %d,%d %d,%d], det_score=%.4f",
                     res.text.c_str(), res.score,
                     res.box.points[0], res.box.points[1], res.box.points[2], res.box.points[3],
                     res.box.points[4], res.box.points[5], res.box.points[6], res.box.points[7], res.box.score);
        }
        ESP_LOGI(TAG, "OCR results: %u, total time (including model loading): %lld ms",
                 static_cast<unsigned>(results.size()), run_ms);
    }

    log_memory("After cleanup");
}
