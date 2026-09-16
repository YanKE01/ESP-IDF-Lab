/* SD card (SDMMC slot 0) performance test for ESP32-P4 and ESP32-S31

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if CONFIG_IDF_TARGET_ESP32P4
#define SD_PWR_LDO_CHANNEL 4
#elif CONFIG_IDF_TARGET_ESP32S31
#define SD_PWR_LDO_CHANNEL 1
#else
#error "This example supports ESP32-P4 and ESP32-S31 only"
#endif

static const char *TAG = "sdcard_perf";

#define SD_MOUNT_POINT      "/sdcard"
#define SDTEST_FILE         SD_MOUNT_POINT "/sdtest.bin"
#define SDTEST_BUF_SIZE     (64 * 1024)
#define SDTEST_FILE_SIZE    (16 * 1024 * 1024)

static sdmmc_card_t *s_card = NULL;

/* Mount the SD card on SDMMC slot 0 in UHS-I SDR50 mode.
 * Use the target's default slot 0 IOs and on-chip LDO for SDMMC IO power. */
static esp_err_t sd_card_mount(void)
{
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;

    ESP_LOGI(TAG, "Initializing SD card (slot 0, UHS-I SDR50)");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_SDR50;
    host.flags &= ~SDMMC_HOST_FLAG_DDR;
#if SOC_SDMMC_IO_UHS_POWER_EXTERNAL
    /* ESP32-S31 uses a separate 1.8 V IO supply for UHS-I operation. */
    host.io_voltage = 1.8f;
#endif
#if CONFIG_IDF_TARGET_ESP32P4
    /* Input sampling delay phase: with the default PHASE_0 (no delay), reads at
     * 100MHz (SDR50) are prone to intermittent CRC errors (0x109).
     * NOTE: SDMMC_DELAY_PHASE_AUTO is not supported on IDF v5.5, so pick a fixed
     * phase manually -- if CRC errors still show up, try PHASE_2 / PHASE_3. */
    host.input_delay_phase = SDMMC_DELAY_PHASE_1;
#endif

    /* LDO VO4 on ESP32-P4, LDO VO1 on ESP32-S31. */
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = SD_PWR_LDO_CHANNEL,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create on-chip LDO power control driver");
        return ret;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    /* The IDF defaults select the target's slot 0 IOMUX pins. */
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.flags |= SDMMC_SLOT_FLAG_UHS1 | SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.width = 4;

    ESP_LOGI(TAG, "Mounting filesystem at %s", SD_MOUNT_POINT);
    ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
        return ret;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    /* Print card info: check "speed" line to confirm SDR50 (100MHz) was negotiated */
    sdmmc_card_print_info(stdout, card);
    s_card = card;

    return ESP_OK;
}

/* Sequential write throughput test */
static void sd_card_write_test(uint8_t *buf)
{
    /* Overwrite the file in place (no O_TRUNC): clusters are allocated only on the
     * first pass, later passes are pure sequential writes without FAT table updates. */
    int fd = open(SDTEST_FILE, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "open for write failed, errno=%d", errno);
        return;
    }
    size_t total = 0;
    int64_t t0 = esp_timer_get_time();
    while (total < SDTEST_FILE_SIZE) {
        ssize_t wr = write(fd, buf, SDTEST_BUF_SIZE);
        if (wr <= 0) {
            ESP_LOGE(TAG, "write failed, errno=%d", errno);
            break;
        }
        total += wr;
    }
    fsync(fd);
    int64_t dt = esp_timer_get_time() - t0;
    close(fd);
    if (total > 0 && dt > 0) {
        ESP_LOGI(TAG, "WRITE %u KB in %lld ms -> %.2f MB/s",
                 (unsigned)(total / 1024), dt / 1000, (double)total / dt);
    }
}

/* Sequential read throughput test */
static void sd_card_read_test(uint8_t *buf)
{
    int fd = open(SDTEST_FILE, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "open for read failed (run a write pass first), errno=%d", errno);
        return;
    }
    size_t total = 0;
    int64_t t0 = esp_timer_get_time();
    while (1) {
        ssize_t rd = read(fd, buf, SDTEST_BUF_SIZE);
        if (rd < 0) {
            ESP_LOGE(TAG, "read failed, errno=%d", errno);
            break;
        }
        if (rd == 0) {  // EOF
            break;
        }
        total += rd;
    }
    int64_t dt = esp_timer_get_time() - t0;
    close(fd);
    if (total > 0 && dt > 0) {
        ESP_LOGI(TAG, "READ  %u KB in %lld ms -> %.2f MB/s",
                 (unsigned)(total / 1024), dt / 1000, (double)total / dt);
    }
}

void app_main(void)
{
    if (sd_card_mount() != ESP_OK) {
        return;
    }

    /* 512-byte aligned DMA-capable buffer: unaligned buffers force the SDMMC driver
     * into the slow chunked-copy path and badly hurt throughput */
    uint8_t *buf = heap_caps_aligned_alloc(512, SDTEST_BUF_SIZE,
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        ESP_LOGE(TAG, "failed to allocate %d bytes", SDTEST_BUF_SIZE);
        return;
    }
    memset(buf, 0xA5, SDTEST_BUF_SIZE);

    ESP_LOGI(TAG, "Starting SD card performance test (%d MB)", SDTEST_FILE_SIZE / (1024 * 1024));
    sd_card_write_test(buf);
    sd_card_read_test(buf);
    ESP_LOGI(TAG, "SD card performance test done");

    heap_caps_free(buf);
}
