#pragma once

#include "esp_err.h"

/**
 * @brief Mounts SPIFFS and prepares the MTP object catalog.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t mtp_storage_init(void);

/**
 * @brief Aborts active transfers and resets the current MTP session.
 */
void mtp_storage_reset(void);
