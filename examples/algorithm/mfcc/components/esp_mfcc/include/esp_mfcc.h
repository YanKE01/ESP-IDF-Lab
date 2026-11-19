/*
 * MFCC (Mel-frequency cepstral coefficients) on top of esp-dsp.
 *
 * The feature definition follows python_speech_features / c_speech_features:
 * pre-emphasis -> framing -> window -> |FFT|^2 / NFFT -> triangular Mel
 * filterbank -> log -> orthonormal DCT-II -> cepstral lifter, optionally with
 * c0 replaced by the log frame energy.
 *
 * The heavy kernels (real FFT, filterbank and DCT dot products) use the
 * esp-dsp implementations, which run on the PIE (SIMD) extension on ESP32-S3
 * and ESP32-P4. Frames are processed one at a time, so memory use does not
 * grow with the input length and no PSRAM is needed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_MFCC_WINDOW_RECT = 0,   /*!< No window (all ones), the python_speech_features default */
    ESP_MFCC_WINDOW_HANN,
    ESP_MFCC_WINDOW_HAMMING,
} esp_mfcc_window_t;

typedef enum {
    ESP_MFCC_IMPL_OPTIMIZED = 0, /*!< esp-dsp kernels optimized for the target (PIE on S3 / P4) */
    ESP_MFCC_IMPL_ANSI,          /*!< esp-dsp plain C kernels, useful as a baseline for benchmarks */
} esp_mfcc_impl_t;

typedef struct {
    int sample_rate;            /*!< Sample rate in Hz */
    int frame_len;              /*!< Samples per frame, e.g. 20 ms at 16 kHz = 320 */
    int frame_step;             /*!< Hop between frames in samples */
    int nfft;                   /*!< FFT size, power of two, >= frame_len */
    int num_filters;            /*!< Number of Mel filters */
    int num_ceps;               /*!< Number of cepstral coefficients kept, <= num_filters */
    float low_freq;             /*!< Lowest filterbank edge in Hz */
    float high_freq;            /*!< Highest filterbank edge in Hz, <= 0 means sample_rate / 2 */
    float preemph;              /*!< Pre-emphasis coefficient, 0 disables it */
    int cep_lifter;             /*!< Cepstral lifter, 0 disables it */
    bool append_energy;         /*!< Replace c0 with log of the frame energy */
    esp_mfcc_window_t window;
    esp_mfcc_impl_t impl;
} esp_mfcc_config_t;

/** 16 kHz, 20 ms frames without overlap, 512-point FFT, 32 filters, 13 coefficients. */
#define ESP_MFCC_DEFAULT_CONFIG() {     \
    .sample_rate = 16000,               \
    .frame_len = 320,                   \
    .frame_step = 320,                  \
    .nfft = 512,                        \
    .num_filters = 32,                  \
    .num_ceps = 13,                     \
    .low_freq = 0,                      \
    .high_freq = 0,                     \
    .preemph = 0.98f,                   \
    .cep_lifter = 32,                   \
    .append_energy = true,              \
    .window = ESP_MFCC_WINDOW_RECT,     \
    .impl = ESP_MFCC_IMPL_OPTIMIZED,    \
}

typedef struct esp_mfcc *esp_mfcc_handle_t;

/**
 * @brief Create an MFCC extractor. Filterbank, DCT matrix and FFT tables are
 *        computed once here.
 */
esp_err_t esp_mfcc_create(const esp_mfcc_config_t *config, esp_mfcc_handle_t *ret_handle);

/** @brief Free an extractor created by esp_mfcc_create(). */
void esp_mfcc_destroy(esp_mfcc_handle_t handle);

/** @brief Number of frames esp_mfcc_compute() produces for num_samples of input. */
size_t esp_mfcc_get_num_frames(esp_mfcc_handle_t handle, size_t num_samples);

/**
 * @brief Compute MFCC for a whole buffer of 16-bit PCM.
 *
 * The last frame is zero padded when the signal does not fill it.
 *
 * @param pcm          Mono 16-bit samples
 * @param num_samples  Number of samples in pcm
 * @param out          Output, num_frames * num_ceps floats, frame-major
 * @param max_frames   Capacity of out in frames
 * @param num_frames   Number of frames written
 */
esp_err_t esp_mfcc_compute(esp_mfcc_handle_t handle, const int16_t *pcm, size_t num_samples,
                           float *out, size_t max_frames, size_t *num_frames);

#ifdef __cplusplus
}
#endif
