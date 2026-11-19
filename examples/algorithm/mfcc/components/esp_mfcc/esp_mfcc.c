#include <float.h>
#include <math.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_dsp.h"
#include "esp_mfcc.h"

static const char *TAG = "esp_mfcc";

/* The PIE paths of dsps_dotprod_f32 need both pointers 16-byte aligned and a
 * length that is a multiple of 4; anything else silently falls back to the
 * scalar loop. Every buffer below is laid out to satisfy that. */
#define MFCC_ALIGN      16
#define ROUND_UP4(x)    (((x) + 3) & ~3)
#define ROUND_DOWN4(x)  ((x) & ~3)

typedef esp_err_t (*dotprod_fn_t)(const float *a, const float *b, float *dest, int len);

typedef struct {
    uint16_t start;     /* first bin, multiple of 4 */
    uint16_t len;       /* number of bins, multiple of 4 */
    uint32_t offset;    /* offset of this filter's weights in fb_weights */
} mel_filter_t;

struct esp_mfcc {
    esp_mfcc_config_t cfg;
    int num_bins;           /* nfft / 2 + 1 */
    int num_bins_pad;       /* num_bins rounded up to 4 */
    int num_filters_pad;    /* num_filters rounded up to 4 */

    float *window;          /* frame_len */
    float *fft_buf;         /* nfft, also used as nfft/2 interleaved complex */
    float *pspec;           /* num_bins_pad, tail kept at 0 */
    mel_filter_t *filters;  /* num_filters */
    float *fb_weights;      /* packed, every filter starts aligned */
    float *log_mel;         /* num_filters_pad, tail kept at 0 */
    float *dct;             /* num_ceps rows of num_filters_pad, DCT scale and lifter folded in */

    dotprod_fn_t dotprod;
    bool ansi;
};

static inline float hz_to_mel(float hz)
{
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static inline float mel_to_hz(float mel)
{
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static void *aligned_calloc(size_t n, size_t size)
{
    return heap_caps_aligned_calloc(MFCC_ALIGN, n, size, MALLOC_CAP_DEFAULT);
}

static esp_err_t build_window(esp_mfcc_handle_t h)
{
    const int n = h->cfg.frame_len;
    h->window = aligned_calloc(n, sizeof(float));
    ESP_RETURN_ON_FALSE(h->window, ESP_ERR_NO_MEM, TAG, "no mem for window");

    switch (h->cfg.window) {
    case ESP_MFCC_WINDOW_HANN:
        dsps_wind_hann_f32(h->window, n);
        break;
    case ESP_MFCC_WINDOW_HAMMING:
        for (int i = 0; i < n; i++) {
            h->window[i] = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / (n - 1));
        }
        break;
    case ESP_MFCC_WINDOW_RECT:
    default:
        for (int i = 0; i < n; i++) {
            h->window[i] = 1.0f;
        }
        break;
    }
    return ESP_OK;
}

/* Triangular Mel filters, same bin placement as python_speech_features.
 * Only the non-zero span of each filter is stored, widened to a multiple of
 * 4 bins so that it can be fed straight to the SIMD dot product. */
static esp_err_t build_filterbank(esp_mfcc_handle_t h)
{
    const esp_mfcc_config_t *c = &h->cfg;
    const int nf = c->num_filters;
    float high = (c->high_freq <= c->low_freq) ? c->sample_rate / 2.0f : c->high_freq;
    float low_mel = hz_to_mel(c->low_freq);
    float high_mel = hz_to_mel(high);

    int bin[nf + 2];
    for (int i = 0; i < nf + 2; i++) {
        float mel = (high_mel - low_mel) / (nf + 1) * i + low_mel;
        bin[i] = (int)floorf((c->nfft + 1) * mel_to_hz(mel) / c->sample_rate);
        if (bin[i] > h->num_bins - 1) {
            bin[i] = h->num_bins - 1;
        }
    }

    h->filters = calloc(nf, sizeof(mel_filter_t));
    ESP_RETURN_ON_FALSE(h->filters, ESP_ERR_NO_MEM, TAG, "no mem for filters");

    size_t total = 0;
    for (int i = 0; i < nf; i++) {
        int start = ROUND_DOWN4(bin[i]);
        int end = ROUND_UP4(bin[i + 2]);
        if (end > h->num_bins_pad) {
            end = h->num_bins_pad;
        }
        h->filters[i].start = start;
        h->filters[i].len = end - start;
        h->filters[i].offset = total;
        total += end - start;
    }

    h->fb_weights = aligned_calloc(total, sizeof(float));
    ESP_RETURN_ON_FALSE(h->fb_weights, ESP_ERR_NO_MEM, TAG, "no mem for filterbank");

    for (int i = 0; i < nf; i++) {
        float *w = h->fb_weights + h->filters[i].offset;
        int s = h->filters[i].start;
        for (int k = bin[i]; k < bin[i + 1]; k++) {
            w[k - s] = (float)(k - bin[i]) / (bin[i + 1] - bin[i]);
        }
        for (int k = bin[i + 1]; k < bin[i + 2]; k++) {
            w[k - s] = (float)(bin[i + 2] - k) / (bin[i + 2] - bin[i + 1]);
        }
    }
    return ESP_OK;
}

/* Orthonormal DCT-II as a num_ceps x num_filters matrix, with the lifter
 * folded into each row so a frame needs only num_ceps dot products. */
static esp_err_t build_dct(esp_mfcc_handle_t h)
{
    const esp_mfcc_config_t *c = &h->cfg;
    const int nf = c->num_filters;
    h->dct = aligned_calloc(c->num_ceps * h->num_filters_pad, sizeof(float));
    ESP_RETURN_ON_FALSE(h->dct, ESP_ERR_NO_MEM, TAG, "no mem for dct");

    for (int j = 0; j < c->num_ceps; j++) {
        double scale = (j == 0) ? sqrt(1.0 / nf) : sqrt(2.0 / nf);
        if (c->cep_lifter > 0) {
            scale *= 1.0 + c->cep_lifter / 2.0 * sin(M_PI * j / c->cep_lifter);
        }
        float *row = h->dct + j * h->num_filters_pad;
        for (int k = 0; k < nf; k++) {
            row[k] = (float)(scale * cos(M_PI * j * (2 * k + 1) / (2.0 * nf)));
        }
    }
    return ESP_OK;
}

static esp_err_t init_fft(esp_mfcc_handle_t h)
{
    const int n_cplx = h->cfg.nfft / 2;
    esp_err_t ret = dsps_fft4r_init_fc32(NULL, n_cplx);
    ESP_RETURN_ON_ERROR(ret, TAG, "dsps_fft4r_init_fc32 failed: %d (check CONFIG_DSP_MAX_FFT_SIZE)", ret);
    /* The twiddle table is global to esp-dsp. If someone initialized it
     * earlier for a smaller FFT, init returns OK without growing it. */
    ESP_RETURN_ON_FALSE(dsps_fft4r_w_table_size >= h->cfg.nfft, ESP_ERR_INVALID_STATE, TAG,
                        "FFT4R table already initialized for a smaller size (%d < %d)",
                        dsps_fft4r_w_table_size, h->cfg.nfft);
    return ESP_OK;
}

esp_err_t esp_mfcc_create(const esp_mfcc_config_t *config, esp_mfcc_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(config && ret_handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    const esp_mfcc_config_t *c = config;
    ESP_RETURN_ON_FALSE(c->nfft >= 8 && (c->nfft & (c->nfft - 1)) == 0, ESP_ERR_INVALID_ARG, TAG,
                        "nfft must be a power of two");
    ESP_RETURN_ON_FALSE(c->frame_len > 0 && c->frame_len <= c->nfft && c->frame_step > 0,
                        ESP_ERR_INVALID_ARG, TAG, "need 0 < frame_len <= nfft and frame_step > 0");
    ESP_RETURN_ON_FALSE(c->num_filters > 0 && c->num_ceps > 0 && c->num_ceps <= c->num_filters,
                        ESP_ERR_INVALID_ARG, TAG, "need 0 < num_ceps <= num_filters");
    ESP_RETURN_ON_FALSE(c->sample_rate > 0, ESP_ERR_INVALID_ARG, TAG, "invalid sample rate");

    esp_mfcc_handle_t h = calloc(1, sizeof(struct esp_mfcc));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no mem for handle");
    h->cfg = *c;
    h->num_bins = c->nfft / 2 + 1;
    h->num_bins_pad = ROUND_UP4(h->num_bins);
    h->num_filters_pad = ROUND_UP4(c->num_filters);
    h->ansi = (c->impl == ESP_MFCC_IMPL_ANSI);
    h->dotprod = h->ansi ? dsps_dotprod_f32_ansi : dsps_dotprod_f32;

    esp_err_t ret = ESP_ERR_NO_MEM;
    h->fft_buf = aligned_calloc(c->nfft, sizeof(float));
    h->pspec = aligned_calloc(h->num_bins_pad, sizeof(float));
    h->log_mel = aligned_calloc(h->num_filters_pad, sizeof(float));
    ESP_GOTO_ON_FALSE(h->fft_buf && h->pspec && h->log_mel, ESP_ERR_NO_MEM, err, TAG, "no mem for buffers");
    ESP_GOTO_ON_ERROR(build_window(h), err, TAG, "window");
    ESP_GOTO_ON_ERROR(build_filterbank(h), err, TAG, "filterbank");
    ESP_GOTO_ON_ERROR(build_dct(h), err, TAG, "dct");
    ESP_GOTO_ON_ERROR(init_fft(h), err, TAG, "fft");

    *ret_handle = h;
    return ESP_OK;
err:
    esp_mfcc_destroy(h);
    return ret;
}

void esp_mfcc_destroy(esp_mfcc_handle_t h)
{
    if (!h) {
        return;
    }
    /* The esp-dsp FFT table is shared with other users, so it is left alone. */
    heap_caps_free(h->window);
    heap_caps_free(h->fft_buf);
    heap_caps_free(h->pspec);
    heap_caps_free(h->fb_weights);
    heap_caps_free(h->log_mel);
    heap_caps_free(h->dct);
    free(h->filters);
    free(h);
}

size_t esp_mfcc_get_num_frames(esp_mfcc_handle_t h, size_t num_samples)
{
    const size_t len = h->cfg.frame_len;
    if (num_samples <= len) {
        return 1;
    }
    return 1 + (num_samples - len + h->cfg.frame_step - 1) / h->cfg.frame_step;
}

/* Pre-emphasis, window and zero padding of one frame into fft_buf. */
static void load_frame(esp_mfcc_handle_t h, const int16_t *pcm, size_t num_samples, size_t base)
{
    const float a = h->cfg.preemph;
    float *buf = h->fft_buf;
    int j = 0;
    for (; j < h->cfg.frame_len; j++) {
        size_t idx = base + j;
        if (idx >= num_samples) {
            break;
        }
        float x = pcm[idx];
        if (idx > 0) {
            x -= a * pcm[idx - 1];
        }
        buf[j] = x * h->window[j];
    }
    memset(buf + j, 0, (h->cfg.nfft - j) * sizeof(float));
}

/* Real FFT of nfft points as an nfft/2-point complex FFT plus a split step,
 * then the power spectrum |X|^2 / nfft. Returns the frame energy. */
static float power_spectrum(esp_mfcc_handle_t h)
{
    const int n_cplx = h->cfg.nfft / 2;
    float *d = h->fft_buf;

    if (h->ansi) {
        dsps_fft4r_fc32_ansi(d, n_cplx);
        dsps_bit_rev4r_direct_fc32_ansi(d, n_cplx);
        dsps_cplx2real_fc32_ansi(d, n_cplx);
    } else {
        dsps_fft4r_fc32(d, n_cplx);
        dsps_bit_rev4r_fc32(d, n_cplx);
        dsps_cplx2real_fc32(d, n_cplx);
    }

    /* cplx2real packs DC in d[0] and Nyquist in d[1]. */
    const float inv_n = 1.0f / h->cfg.nfft;
    float *p = h->pspec;
    p[0] = d[0] * d[0] * inv_n;
    p[n_cplx] = d[1] * d[1] * inv_n;
    float energy = p[0] + p[n_cplx];
    for (int k = 1; k < n_cplx; k++) {
        float re = d[2 * k];
        float im = d[2 * k + 1];
        p[k] = (re * re + im * im) * inv_n;
        energy += p[k];
    }
    return energy;
}

esp_err_t esp_mfcc_compute(esp_mfcc_handle_t h, const int16_t *pcm, size_t num_samples,
                           float *out, size_t max_frames, size_t *num_frames)
{
    ESP_RETURN_ON_FALSE(h && pcm && out && num_frames, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    const size_t n = esp_mfcc_get_num_frames(h, num_samples);
    ESP_RETURN_ON_FALSE(n <= max_frames, ESP_ERR_INVALID_SIZE, TAG,
                        "output holds %u frames, need %u", (unsigned)max_frames, (unsigned)n);

    const esp_mfcc_config_t *c = &h->cfg;
    for (size_t f = 0; f < n; f++) {
        load_frame(h, pcm, num_samples, f * c->frame_step);
        float energy = power_spectrum(h);

        for (int i = 0; i < c->num_filters; i++) {
            const mel_filter_t *flt = &h->filters[i];
            float e = 0;
            h->dotprod(h->fb_weights + flt->offset, h->pspec + flt->start, &e, flt->len);
            h->log_mel[i] = logf(e > 0 ? e : FLT_MIN);
        }

        float *cep = out + f * c->num_ceps;
        for (int j = 0; j < c->num_ceps; j++) {
            h->dotprod(h->dct + j * h->num_filters_pad, h->log_mel, &cep[j], h->num_filters_pad);
        }
        if (c->append_energy) {
            cep[0] = logf(energy > 0 ? energy : FLT_MIN);
        }
    }

    *num_frames = n;
    return ESP_OK;
}
