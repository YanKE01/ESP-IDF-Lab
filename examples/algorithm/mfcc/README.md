# MFCC with esp-dsp

Extracts MFCC (Mel-frequency cepstral coefficients) from a built-in 1.47 s speech clip using [esp-dsp](https://github.com/espressif/esp-dsp). On ESP32-S3 and ESP32-P4 the FFT and dot-product kernels run on the PIE (SIMD) extension.

Supported targets: ESP32-S3, ESP32-P4 (other targets build too, with esp-dsp's generic kernels). No PSRAM is needed.

## Pipeline

```text
int16 PCM ─► pre-emphasis ─► frame + window ─► real FFT ─► |X|² / N ─► Mel filterbank ─► log ─► DCT-II + lifter ─► MFCC
                                               dsps_fft4r                dsps_dotprod              dsps_dotprod
                                               + cplx2real
```

| Step | esp-dsp function | Notes |
| --- | --- | --- |
| Real FFT (512 points) | `dsps_fft4r_fc32` + `dsps_bit_rev4r_fc32` + `dsps_cplx2real_fc32` | Computed as a 256-point complex FFT plus a split step |
| Mel filterbank | `dsps_dotprod_f32` | Only the non-zero span of each triangular filter is stored, padded to multiples of 4 bins |
| DCT-II + lifter | `dsps_dotprod_f32` | Orthonormal DCT matrix with the lifter folded into each row |

The PIE paths of `dsps_dotprod_f32` require 16-byte aligned buffers and lengths that are a multiple of 4, otherwise esp-dsp silently falls back to the scalar code. The component allocates and pads every buffer to meet that.

Frames are processed one at a time, so memory use is a few KB regardless of the input length.

## Component API

`components/esp_mfcc` can be copied into other projects:

```c
#include "esp_mfcc.h"

esp_mfcc_config_t cfg = ESP_MFCC_DEFAULT_CONFIG();   // 16 kHz, 20 ms frames, 512 FFT, 32 filters, 13 ceps
esp_mfcc_handle_t mfcc;
ESP_ERROR_CHECK(esp_mfcc_create(&cfg, &mfcc));

size_t max_frames = esp_mfcc_get_num_frames(mfcc, num_samples);
float *feat = malloc(max_frames * cfg.num_ceps * sizeof(float));
size_t frames;
ESP_ERROR_CHECK(esp_mfcc_compute(mfcc, pcm, num_samples, feat, max_frames, &frames));

esp_mfcc_destroy(mfcc);
```

The feature definition matches [python_speech_features](https://github.com/jameslyons/python_speech_features) / [c_speech_features](https://github.com/Cwiiis/c_speech_features), which earlier versions of this example used.

## What the example does

1. Computes MFCC 10 times with `ESP_MFCC_IMPL_ANSI` (esp-dsp plain C kernels) and 10 times with `ESP_MFCC_IMPL_OPTIMIZED`, keeping the fastest run of each.
2. Compares both results with `main/mfcc_reference.h`, values produced on a PC by the original c_speech_features implementation.

## Build and run

```bash
idf.py set-target esp32s3   # or esp32p4
idf.py build flash monitor
```

Output on an ESP32-S3 at 240 MHz (ESP-IDF v5.5, esp-dsp 1.8.2):

```text
MFCC of 1472 ms audio, 74 frames x 13 coefficients (first 5 frames):
frame  0:   15.325  -18.265   -3.178    4.495   -4.682   16.872  -13.847    9.026  -16.593   -2.242   11.617   21.323    5.578
...

implementation          time (us)       cycles        RTF    max |err|
esp-dsp ANSI C              37101      8904428     0.0252     5.91e-05
esp-dsp optimized           16076      3858458     0.0109     5.34e-05
speedup: 2.31x (best of 10 runs, RTF = processing time / audio length)
reference check: PASS (tolerance 1e-02)
```

## Performance

Same 1.47 s clip on the same ESP32-S3 board at 240 MHz:

| Implementation | Time | vs. previous version |
| --- | ---: | ---: |
| Previous version (c_speech_features + kiss_fft, buffers in PSRAM) | 246.8 ms | 1x |
| esp_mfcc, esp-dsp ANSI C kernels | 37.1 ms | 6.7x |
| esp_mfcc, esp-dsp PIE kernels | 16.1 ms | 15.4x |

The previous version was timed as a single call that also built the filterbank and DCT tables; esp_mfcc builds those once in `esp_mfcc_create()`, outside the timed loop. The gain over the previous version has not been profiled step by step; the likely reasons are keeping all buffers in internal RAM instead of PSRAM, using float instead of double in the DCT (the S3 FPU is single precision only), and multiplying only the non-zero part of each filter. Switching the same code from the esp-dsp ANSI kernels to the PIE kernels gives a measured 2.3x.
