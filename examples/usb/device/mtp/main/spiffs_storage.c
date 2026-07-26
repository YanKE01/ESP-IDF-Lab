#include "spiffs_storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_spiffs.h"

#define STORAGE_MOUNT_PATH "/spiffs"
#define STORAGE_PARTITION_LABEL "storage"
#define STORAGE_PATH_MAX (sizeof(STORAGE_MOUNT_PATH) + SPIFFS_STORAGE_FILENAME_MAX + 1)

#ifdef CONFIG_MTP_SPIFFS_FORMAT_IF_MOUNT_FAILED
#define SPIFFS_FORMAT_IF_MOUNT_FAILED true
#else
#define SPIFFS_FORMAT_IF_MOUNT_FAILED false
#endif

static const char *TAG = "SPIFFS_STORAGE";

static bool storage_make_path(const char *name, char *path, size_t path_size)
{
    if (name == NULL || name[0] == '\0' || strlen(name) > SPIFFS_STORAGE_FILENAME_MAX || strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    int length = snprintf(path, path_size, "%s/%s", STORAGE_MOUNT_PATH, name);
    return length > 0 && (size_t)length < path_size;
}

static const char *storage_entry_name(const char *path)
{
    const char *separator = strrchr(path, '/');
    return separator == NULL ? path : separator + 1;
}

static FILE *storage_file(spiffs_storage_file_t *file)
{
    return (FILE *)file;
}

static void storage_create_readme(void)
{
    static const char contents[] = "ESP32 TinyUSB MTP storage backed by SPIFFS.\n";
    char path[STORAGE_PATH_MAX];
    if (!storage_make_path("readme.txt", path, sizeof(path)) || access(path, F_OK) == 0) {
        return;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Failed to create readme.txt: errno=%d", errno);
        return;
    }
    size_t written = fwrite(contents, 1, sizeof(contents) - 1, file);
    if (written != sizeof(contents) - 1) {
        ESP_LOGW(TAG, "Failed to write complete readme.txt");
    }
    fclose(file);
}

esp_err_t spiffs_storage_init(void)
{
    const esp_vfs_spiffs_conf_t configuration = {
        .base_path = STORAGE_MOUNT_PATH,
        .partition_label = STORAGE_PARTITION_LABEL,
        .max_files = CONFIG_MTP_SPIFFS_MAX_OPEN_FILES,
        .format_if_mount_failed = SPIFFS_FORMAT_IF_MOUNT_FAILED,
    };
    esp_err_t error = esp_vfs_spiffs_register(&configuration);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(error));
        return error;
    }

    storage_create_readme();
    size_t total_bytes;
    size_t used_bytes;
    if (!spiffs_storage_get_capacity(&total_bytes, &used_bytes)) {
        spiffs_storage_deinit();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SPIFFS mounted: total=%zu, used=%zu", total_bytes, used_bytes);
    return ESP_OK;
}

void spiffs_storage_deinit(void)
{
    esp_vfs_spiffs_unregister(STORAGE_PARTITION_LABEL);
}

bool spiffs_storage_get_capacity(size_t *total_bytes, size_t *used_bytes)
{
    esp_err_t error = esp_spiffs_info(STORAGE_PARTITION_LABEL, total_bytes, used_bytes);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query SPIFFS: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool spiffs_storage_enumerate(spiffs_storage_entry_cb_t callback, void *context)
{
    if (callback == NULL) {
        return false;
    }

    DIR *directory = opendir(STORAGE_MOUNT_PATH);
    if (directory == NULL) {
        ESP_LOGE(TAG, "Failed to open %s: errno=%d", STORAGE_MOUNT_PATH, errno);
        return false;
    }

    struct dirent *directory_entry;
    while ((directory_entry = readdir(directory)) != NULL) {
        const char *name = storage_entry_name(directory_entry->d_name);
        char path[STORAGE_PATH_MAX];
        struct stat file_stat;
        if (!storage_make_path(name, path, sizeof(path)) || stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            continue;
        }

        spiffs_storage_entry_t entry = {
            .size = (uint32_t)file_stat.st_size,
        };
        strlcpy(entry.name, name, sizeof(entry.name));
        callback(&entry, context);
    }
    closedir(directory);
    return true;
}

bool spiffs_storage_get_file_size(const char *name, uint32_t *size)
{
    char path[STORAGE_PATH_MAX];
    struct stat file_stat;
    if (size == NULL || !storage_make_path(name, path, sizeof(path)) || stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        return false;
    }
    *size = (uint32_t)file_stat.st_size;
    return true;
}

spiffs_storage_file_t *spiffs_storage_open(const char *name, const char *mode)
{
    char path[STORAGE_PATH_MAX];
    if (mode == NULL || !storage_make_path(name, path, sizeof(path))) {
        return NULL;
    }
    return (spiffs_storage_file_t *)fopen(path, mode);
}

bool spiffs_storage_seek(spiffs_storage_file_t *file, uint32_t offset)
{
    return file != NULL && fseek(storage_file(file), (long)offset, SEEK_SET) == 0;
}

size_t spiffs_storage_read(spiffs_storage_file_t *file, void *buffer, size_t size)
{
    return file == NULL ? 0 : fread(buffer, 1, size, storage_file(file));
}

size_t spiffs_storage_write(spiffs_storage_file_t *file, const void *buffer, size_t size)
{
    return file == NULL ? 0 : fwrite(buffer, 1, size, storage_file(file));
}

bool spiffs_storage_close(spiffs_storage_file_t *file)
{
    return file != NULL && fclose(storage_file(file)) == 0;
}

bool spiffs_storage_remove(const char *name)
{
    char path[STORAGE_PATH_MAX];
    return storage_make_path(name, path, sizeof(path)) && unlink(path) == 0;
}
