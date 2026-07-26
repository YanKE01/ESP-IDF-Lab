#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#if CONFIG_SPIFFS_OBJ_NAME_LEN < 4
#error CONFIG_SPIFFS_OBJ_NAME_LEN must be at least 4
#endif

#define SPIFFS_STORAGE_FILENAME_MAX (CONFIG_SPIFFS_OBJ_NAME_LEN - 2)

typedef struct spiffs_storage_file spiffs_storage_file_t;

typedef struct {
    char name[SPIFFS_STORAGE_FILENAME_MAX + 1];
    uint32_t size;
} spiffs_storage_entry_t;

typedef void (*spiffs_storage_entry_cb_t)(const spiffs_storage_entry_t *entry, void *context);

/**
 * @brief Mounts SPIFFS and creates the default file when needed.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t spiffs_storage_init(void);

/**
 * @brief Unmounts the SPIFFS partition.
 */
void spiffs_storage_deinit(void);

/**
 * @brief Returns the total and used bytes reported by SPIFFS.
 */
bool spiffs_storage_get_capacity(size_t *total_bytes, size_t *used_bytes);

/**
 * @brief Calls the callback once for every regular file in the storage root.
 */
bool spiffs_storage_enumerate(spiffs_storage_entry_cb_t callback, void *context);

/**
 * @brief Returns the current size of a file.
 */
bool spiffs_storage_get_file_size(const char *name, uint32_t *size);

/**
 * @brief Opens a file using a standard C file mode.
 */
spiffs_storage_file_t *spiffs_storage_open(const char *name, const char *mode);

/**
 * @brief Moves the file position to an absolute byte offset.
 */
bool spiffs_storage_seek(spiffs_storage_file_t *file, uint32_t offset);

/**
 * @brief Reads bytes from an open file.
 */
size_t spiffs_storage_read(spiffs_storage_file_t *file, void *buffer, size_t size);

/**
 * @brief Writes bytes to an open file.
 */
size_t spiffs_storage_write(spiffs_storage_file_t *file, const void *buffer, size_t size);

/**
 * @brief Closes an open file.
 */
bool spiffs_storage_close(spiffs_storage_file_t *file);

/**
 * @brief Deletes a file from SPIFFS.
 */
bool spiffs_storage_remove(const char *name);
