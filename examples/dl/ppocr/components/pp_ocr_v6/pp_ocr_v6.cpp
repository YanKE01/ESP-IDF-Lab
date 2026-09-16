#include "pp_ocr_v6.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pp_ocr_v6_det_postprocessor.hpp"
#include "pp_ocr_v6_image_preprocessor.hpp"
#include "pp_ocr_v6_rec_postprocessor.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_PP_OCR_V6_MODEL_IN_FLASH_RODATA
extern const uint8_t pp_ocr_v6_espdl[] asm("_binary_pp_ocr_v6_espdl_start");
static const char *model_path = (const char *)pp_ocr_v6_espdl;
#elif CONFIG_PP_OCR_V6_MODEL_IN_FLASH_PARTITION
static const char *model_path = "pp_ocr_v6";
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif

static const char *TAG = "pp_ocr_v6";

namespace pp_ocr_v6 {
static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "%s: PSRAM free=%u, largest SIMD block=%u", stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD)));
}

static bool valid_image(const dl::image::img_t &img)
{
    return img.data && img.width > 0 && img.height > 0 && img.pix_type == dl::image::DL_IMAGE_PIX_TYPE_RGB888;
}

static dl::Model *load_model(const char *model_name, bool param_copy)
{
    const int64_t start = esp_timer_get_time();
    log_memory(model_name);
#if CONFIG_PP_OCR_V6_MODEL_IN_SDCARD
    auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_PP_OCR_V6_MODEL_SDCARD_DIR / model_name;
    auto *model = new (std::nothrow) dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#else
    auto *model = new (std::nothrow) dl::Model(
        model_path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_PP_OCR_V6_MODEL_LOCATION),
        0, dl::MEMORY_MANAGER_GREEDY, nullptr, param_copy);
#endif
    // Model::build() does not return allocation failures. Check tensors before
    // minimize() or ImagePreprocessor can dereference an unallocated input.
    auto *input = model ? model->get_input() : nullptr;
    bool ready = input && input->data && input->shape.size() == 4 && input->shape[0] == 1 &&
                 input->shape[1] > 0 && input->shape[2] > 0 && input->shape[3] == 3 &&
                 (input->dtype == dl::DATA_TYPE_INT8 || input->dtype == dl::DATA_TYPE_INT16);
    if (ready) {
        const auto &outputs = model->get_outputs();
        ready = !outputs.empty() && std::all_of(outputs.begin(), outputs.end(), [](const auto & entry) {
            return entry.second && entry.second->data;
        });
    }
    if (!ready) {
        ESP_LOGE(TAG, "Failed to load/build %s: input or output tensors are unavailable", model_name);
        log_memory("Model load failed");
        delete model;
        return nullptr;
    }
    model->minimize();
    ESP_LOGI(TAG, "%s loaded in %lld ms", model_name, (esp_timer_get_time() - start) / 1000);
    return model;
}

Det::Det(const char *model_name) :
    m_model(nullptr), m_image_preprocessor(nullptr), m_postprocessor(nullptr), m_letterbox_img({}), m_resize_scale(1.0f)
{
    // Keep detection weights in flash to leave PSRAM for its large workspace.
    m_model = load_model(model_name, false);
    if (!m_model) {
        return;
    }

    auto *model_input = m_model->get_input();
    int model_h = model_input->shape[1];
    int model_w = model_input->shape[2];
    void *buf = heap_caps_aligned_alloc(16, model_w * model_h * 3, MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = heap_caps_aligned_alloc(16, model_w * model_h * 3, MALLOC_CAP_DEFAULT);
    }
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate detection letterbox buffer (%d bytes)", model_w * model_h * 3);
        return;
    }
    m_letterbox_img = {.data = buf,
                       .width = static_cast<uint16_t>(model_w),
                       .height = static_cast<uint16_t>(model_h),
                       .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888
                      };
    m_image_preprocessor = new (std::nothrow)
    dl::image::ImagePreprocessor(m_model, {123.675f, 116.28f, 103.53f}, {58.395f, 57.12f, 57.375f}, true);
    m_postprocessor = new (std::nothrow) DetPostprocessor(m_model);
}

Det::~Det()
{
    if (m_letterbox_img.data) {
        heap_caps_free(m_letterbox_img.data);
    }
    delete m_postprocessor;
    delete m_image_preprocessor;
    delete m_model;
}

bool Det::is_ready() const
{
    return m_model && m_image_preprocessor && m_postprocessor && m_letterbox_img.data;
}

std::vector<TextBox> Det::run(const dl::image::img_t &img)
{
    if (!is_ready() || !valid_image(img)) {
        ESP_LOGE(TAG, "Detection model is not ready or image is invalid");
        return {};
    }
    log_memory("Detection preprocess");
    DL_LOG_INFER_LATENCY_INIT();
    DL_LOG_INFER_LATENCY_START();
    int model_w = m_letterbox_img.width;
    int model_h = m_letterbox_img.height;
    float scale_x = static_cast<float>(model_w) / img.width;
    float scale_y = static_cast<float>(model_h) / img.height;
    m_resize_scale = std::min(scale_x, scale_y);
    int resize_w = std::max(1, static_cast<int>(std::round(img.width * m_resize_scale)));
    int resize_h = std::max(1, static_cast<int>(std::round(img.height * m_resize_scale)));
    resize_w = std::min(resize_w, model_w);
    resize_h = std::min(resize_h, model_h);
    bilinear_letterbox_top_left(img, m_letterbox_img, resize_w, resize_h);
    m_image_preprocessor->preprocess(m_letterbox_img);
    DL_LOG_INFER_LATENCY_END_PRINT("det", "pre");

    DL_LOG_INFER_LATENCY_START();
    log_memory("Detection inference");
    m_model->run();
    DL_LOG_INFER_LATENCY_END_PRINT("det", "model");

    DL_LOG_INFER_LATENCY_START();
    log_memory("Detection postprocess");
    auto boxes = m_postprocessor->postprocess(img, m_resize_scale, model_w, model_h);
    DL_LOG_INFER_LATENCY_END_PRINT("det", "post");

    return boxes;
}

Rec::Rec(const char *model_name) : m_model(nullptr), m_image_preprocessor(nullptr), m_postprocessor(nullptr)
{
    m_model = load_model(model_name, true);
    if (!m_model) {
        return;
    }
    m_image_preprocessor =
        new (std::nothrow) dl::image::ImagePreprocessor(m_model, {127.5f, 127.5f, 127.5f}, {127.5f, 127.5f, 127.5f}, true);
    m_postprocessor = new (std::nothrow) RecCTCPostprocessor(m_model);
}

Rec::~Rec()
{
    delete m_postprocessor;
    delete m_image_preprocessor;
    delete m_model;
}

bool Rec::is_ready() const
{
    return m_model && m_image_preprocessor && m_postprocessor;
}

float Rec::input_aspect_ratio() const
{
    if (!m_image_preprocessor) {
        return 0.0f;
    }
    auto *input = m_image_preprocessor->get_model_input();
    if (!input || input->shape.size() < 4) {
        return 0.0f;
    }
    int h = input->shape[1];
    int w = input->shape[2];
    if (h <= 0 || w <= 0) {
        return 0.0f;
    }
    return static_cast<float>(w) / static_cast<float>(h);
}

std::string Rec::run(const dl::image::img_t &img, const TextBox &box, float *score)
{
    if (score) {
        *score = 0.0f;
    }
    if (!is_ready() || !valid_image(img)) {
        ESP_LOGE(TAG, "Recognition model is not ready or image is invalid");
        return {};
    }
    DL_LOG_INFER_LATENCY_INIT();
    DL_LOG_INFER_LATENCY_START();
    dl::image::img_t crop = get_rotate_crop_image(img, box);
    if (!crop.data) {
        if (score) {
            *score = 0.0f;
        }
        return {};
    }

    dl::TensorBase *model_input = m_image_preprocessor->get_model_input();
    bool input_ready = resize_norm_img(crop, model_input);
    heap_caps_free(crop.data);
    if (!input_ready) {
        if (score) {
            *score = 0.0f;
        }
        return {};
    }
    DL_LOG_INFER_LATENCY_END_PRINT("rec", "pre");

    DL_LOG_INFER_LATENCY_START();
    m_model->run();
    DL_LOG_INFER_LATENCY_END_PRINT("rec", "model");

    DL_LOG_INFER_LATENCY_START();
    std::string text = m_postprocessor->postprocess(score);
    DL_LOG_INFER_LATENCY_END_PRINT("rec", "post");

    return text;
}

static const char *rec_model_name_for(RecModel type)
{
    switch (type) {
    case RecModel::PP_OCR_V6_REC_S16:
#if CONFIG_FLASH_PP_OCR_V6_REC_S16 || CONFIG_PP_OCR_V6_MODEL_IN_SDCARD
        return "pp_ocr_v6_rec_s16.espdl";
#else
        ESP_LOGE(TAG, "pp_ocr_v6_rec_s16 not flashed; falling back to pp_ocr_v6_rec_s8.");
        return "pp_ocr_v6_rec_s8.espdl";
#endif
    case RecModel::PP_OCR_V6_REC_S8:
    default:
        return "pp_ocr_v6_rec_s8.espdl";
    }
}

PPOCRV6::PPOCRV6(RecMode rec_mode, RecModel rec_type) :
    m_rec_type(rec_type),
    m_last_error(ESP_OK),
    m_rec_score_threshold(default_rec_score_threshold)
{
    if (rec_mode == RecMode::Dual) {
        ESP_LOGW(TAG, "Only short recognition is bundled; falling back to SHORT.");
    }
    ESP_LOGI(TAG, "Staged OCR: detection, then %s", rec_model_name_for(rec_type));
}

PPOCRV6::~PPOCRV6() = default;

std::vector<OCRResult> PPOCRV6::run(const dl::image::img_t &img)
{
    m_last_error = ESP_OK;
    if (!valid_image(img)) {
        m_last_error = ESP_ERR_INVALID_ARG;
        return {};
    }
    std::vector<TextBox> boxes;
    {
        Det det;
        if (!det.is_ready()) {
            m_last_error = ESP_ERR_NO_MEM;
            return {};
        }
        boxes = det.run(img);
    } // Release the detection workspace before loading recognition.
    log_memory("Detection released");
    if (boxes.empty()) {
        return {};
    }

    Rec rec(rec_model_name_for(m_rec_type));
    if (!rec.is_ready()) {
        m_last_error = ESP_ERR_NO_MEM;
        return {};
    }
    log_memory("Recognition inference");
    std::vector<OCRResult> results;
    results.reserve(boxes.size());
    for (const auto &box : boxes) {
        float rec_score = 0.0f;

        auto text = rec.run(img, box, &rec_score);
        if (rec_score >= m_rec_score_threshold) {
            results.push_back({box, text, rec_score});
        }
        vTaskDelay(1);
    }
    return results;
}

} // namespace pp_ocr_v6
