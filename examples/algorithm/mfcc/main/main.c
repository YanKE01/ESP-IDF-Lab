/*
 * MFCC feature extraction with esp-dsp.
 *
 * Computes MFCC of a built-in speech clip twice, once with the esp-dsp
 * kernels optimized for the target (PIE/SIMD on ESP32-S3 and ESP32-P4) and
 * once with the esp-dsp plain C kernels, then checks both against reference
 * values from the original c_speech_features implementation.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mfcc.h"
#include "audio_sample.h"
#include "mfcc_reference.h"

static const char *TAG = "mfcc";

#define NUM_SAMPLES     (sizeof(audio_sample) / sizeof(audio_sample[0]))
#define BENCH_RUNS      10
#define MAX_ABS_ERROR   1e-2f

typedef struct {
    int64_t best_us;
    uint32_t best_cycles;
    float max_err;
} run_result_t;

static esp_err_t run(esp_mfcc_impl_t impl, float *out, size_t max_frames, run_result_t *res)
{
    esp_mfcc_config_t cfg = ESP_MFCC_DEFAULT_CONFIG();
    cfg.impl = impl;

    esp_mfcc_handle_t mfcc = NULL;
    ESP_RETURN_ON_ERROR(esp_mfcc_create(&cfg, &mfcc), TAG, "create failed");

    size_t frames = 0;
    res->best_us = INT64_MAX;
    res->best_cycles = UINT32_MAX;
    for (int i = 0; i < BENCH_RUNS; i++) {
        uint32_t c0 = esp_cpu_get_cycle_count();
        int64_t t0 = esp_timer_get_time();
        esp_err_t ret = esp_mfcc_compute(mfcc, audio_sample, NUM_SAMPLES, out, max_frames, &frames);
        int64_t us = esp_timer_get_time() - t0;
        uint32_t cycles = esp_cpu_get_cycle_count() - c0;
        if (ret != ESP_OK) {
            esp_mfcc_destroy(mfcc);
            return ret;
        }
        if (us < res->best_us) {
            res->best_us = us;
        }
        if (cycles < res->best_cycles) {
            res->best_cycles = cycles;
        }
    }
    esp_mfcc_destroy(mfcc);

    ESP_RETURN_ON_FALSE(frames == MFCC_REF_FRAMES, ESP_FAIL, TAG,
                        "got %u frames, reference has %d", (unsigned)frames, MFCC_REF_FRAMES);
    res->max_err = 0;
    for (size_t i = 0; i < frames * MFCC_REF_CEPS; i++) {
        float err = fabsf(out[i] - mfcc_reference[i]);
        if (err > res->max_err) {
            res->max_err = err;
        }
    }
    return ESP_OK;
}

static void print_frames(const float *mfcc, int num_frames, int num_ceps)
{
    for (int i = 0; i < num_frames; i++) {
        printf("frame %2d:", i);
        for (int j = 0; j < num_ceps; j++) {
            printf(" %8.3f", mfcc[i * num_ceps + j]);
        }
        printf("\n");
    }
}

void app_main(void)
{
    const size_t max_frames = MFCC_REF_FRAMES;
    float *out = malloc(max_frames * MFCC_REF_CEPS * sizeof(float));
    if (!out) {
        ESP_LOGE(TAG, "no memory for output");
        return;
    }

    run_result_t opt = {0}, ansi = {0};
    if (run(ESP_MFCC_IMPL_ANSI, out, max_frames, &ansi) != ESP_OK ||
            run(ESP_MFCC_IMPL_OPTIMIZED, out, max_frames, &opt) != ESP_OK) {
        free(out);
        return;
    }

    const float audio_ms = NUM_SAMPLES * 1000.0f / 16000;
    printf("\nMFCC of %.0f ms audio, %d frames x %d coefficients (first 5 frames):\n",
           audio_ms, MFCC_REF_FRAMES, MFCC_REF_CEPS);
    print_frames(out, 5, MFCC_REF_CEPS);

    printf("\n%-22s %10s %12s %10s %12s\n", "implementation", "time (us)", "cycles", "RTF", "max |err|");
    printf("%-22s %10lld %12lu %10.4f %12.2e\n", "esp-dsp ANSI C", ansi.best_us,
           (unsigned long)ansi.best_cycles, ansi.best_us / 1000.0f / audio_ms, ansi.max_err);
    printf("%-22s %10lld %12lu %10.4f %12.2e\n", "esp-dsp optimized", opt.best_us,
           (unsigned long)opt.best_cycles, opt.best_us / 1000.0f / audio_ms, opt.max_err);
    printf("speedup: %.2fx (best of %d runs, RTF = processing time / audio length)\n",
           (float)ansi.best_cycles / opt.best_cycles, BENCH_RUNS);

    bool ok = opt.max_err < MAX_ABS_ERROR && ansi.max_err < MAX_ABS_ERROR;
    printf("reference check: %s (tolerance %.0e)\n", ok ? "PASS" : "FAIL", MAX_ABS_ERROR);
    free(out);
}
