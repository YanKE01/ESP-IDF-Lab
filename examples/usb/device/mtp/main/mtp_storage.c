#include "mtp_storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "mtp_state.h"
#include "spiffs_storage.h"
#include "tusb.h"

#define STORAGE_ID 0x00010001u
#define MTP_FILENAME_MAX SPIFFS_STORAGE_FILENAME_MAX
#define DEVICE_INFO_MANUFACTURER "TinyUSB"
#define DEVICE_INFO_MODEL "ESP SPIFFS MTP"
#define DEVICE_INFO_VERSION "1.0"
#define DEVICE_INFO_SERIAL "123456"
#define DEVICE_FRIENDLY_NAME "ESP SPIFFS MTP"
#define STORAGE_DESCRIPTION { 'I', 'n', 't', 'e', 'r', 'n', 'a', 'l', ' ', 'S', 't', 'o', 'r', 'a', 'g', 'e', 0 }
#define VOLUME_IDENTIFIER { 'E', 'S', 'P', '3', '2', 0 }

enum {
    STORAGE_DESCRIPTION_LEN = TU_ARRAY_SIZE((uint16_t[])STORAGE_DESCRIPTION),
    VOLUME_IDENTIFIER_LEN = TU_ARRAY_SIZE((uint16_t[])VOLUME_IDENTIFIER),
};

typedef MTP_STORAGE_INFO_STRUCT(STORAGE_DESCRIPTION_LEN, VOLUME_IDENTIFIER_LEN) mtp_storage_info_t;

typedef struct {
    bool used;
    char name[MTP_FILENAME_MAX + 1];
    uint32_t size;
    uint16_t format;
} mtp_object_t;

typedef enum {
    TRANSFER_NONE,
    TRANSFER_READ,
    TRANSFER_WRITE,
} transfer_type_t;

typedef struct {
    transfer_type_t type;
    spiffs_storage_file_t *file;
    uint32_t handle;
    uint32_t expected_bytes;
    uint32_t transferred_bytes;
    bool failed;
    char name[MTP_FILENAME_MAX + 1];
} mtp_transfer_t;

typedef struct {
    mtp_object_t objects[CONFIG_MTP_MAX_FILE_COUNT];
    mtp_transfer_t transfer;
} mtp_storage_context_t;

typedef int32_t (*mtp_operation_handler_t)(tud_mtp_cb_data_t *cb_data);

typedef struct {
    uint16_t operation;
    mtp_operation_handler_t handler;
} mtp_operation_entry_t;

static const char *TAG = "MTP_STORAGE";
static mtp_storage_context_t s_storage;

static int32_t mtp_get_device_info(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_open_close_session(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_storage_ids(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_storage_info(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_device_property(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_num_objects(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_object_handles(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_object_info(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_object(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_get_partial_object(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_delete_object(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_send_object_info(tud_mtp_cb_data_t *cb_data);
static int32_t mtp_send_object(tud_mtp_cb_data_t *cb_data);

static const mtp_operation_entry_t s_operation_handlers[] = {
    { MTP_OP_GET_DEVICE_INFO, mtp_get_device_info },
    { MTP_OP_OPEN_SESSION, mtp_open_close_session },
    { MTP_OP_CLOSE_SESSION, mtp_open_close_session },
    { MTP_OP_GET_STORAGE_IDS, mtp_get_storage_ids },
    { MTP_OP_GET_STORAGE_INFO, mtp_get_storage_info },
    { MTP_OP_GET_DEVICE_PROP_DESC, mtp_get_device_property },
    { MTP_OP_GET_DEVICE_PROP_VALUE, mtp_get_device_property },
    { MTP_OP_GET_NUM_OBJECTS, mtp_get_num_objects },
    { MTP_OP_GET_OBJECT_HANDLES, mtp_get_object_handles },
    { MTP_OP_GET_OBJECT_INFO, mtp_get_object_info },
    { MTP_OP_GET_OBJECT, mtp_get_object },
    { MTP_OP_GET_PARTIAL_OBJECT, mtp_get_partial_object },
    { MTP_OP_DELETE_OBJECT, mtp_delete_object },
    { MTP_OP_SEND_OBJECT_INFO, mtp_send_object_info },
    { MTP_OP_SEND_OBJECT, mtp_send_object },
};

static uint16_t object_format_from_name(const char *name)
{
    const char *extension = strrchr(name, '.');
    if (extension == NULL) {
        return MTP_OBJ_FORMAT_UNDEFINED;
    }
    if (strcasecmp(extension, ".txt") == 0) {
        return MTP_OBJ_FORMAT_TEXT;
    }
    if (strcasecmp(extension, ".htm") == 0 || strcasecmp(extension, ".html") == 0) {
        return MTP_OBJ_FORMAT_HTML;
    }
    if (strcasecmp(extension, ".jpg") == 0 || strcasecmp(extension, ".jpeg") == 0) {
        return MTP_OBJ_FORMAT_EXIF_JPEG;
    }
    if (strcasecmp(extension, ".png") == 0) {
        return MTP_OBJ_FORMAT_PNG;
    }
    if (strcasecmp(extension, ".gif") == 0) {
        return MTP_OBJ_FORMAT_GIF;
    }
    if (strcasecmp(extension, ".bmp") == 0) {
        return MTP_OBJ_FORMAT_BMP;
    }
    if (strcasecmp(extension, ".mp3") == 0) {
        return MTP_OBJ_FORMAT_MP3;
    }
    if (strcasecmp(extension, ".wav") == 0) {
        return MTP_OBJ_FORMAT_WAV;
    }
    if (strcasecmp(extension, ".mp4") == 0) {
        return MTP_OBJ_FORMAT_MP4;
    }
    return MTP_OBJ_FORMAT_UNDEFINED;
}

static mtp_object_t *catalog_get(uint32_t handle)
{
    if (handle == 0 || handle > CONFIG_MTP_MAX_FILE_COUNT || !s_storage.objects[handle - 1].used) {
        return NULL;
    }
    return &s_storage.objects[handle - 1];
}

static uint32_t catalog_find(const char *name)
{
    for (uint32_t i = 0; i < CONFIG_MTP_MAX_FILE_COUNT; i++) {
        if (s_storage.objects[i].used && strcmp(s_storage.objects[i].name, name) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static uint32_t catalog_allocate(void)
{
    for (uint32_t i = 0; i < CONFIG_MTP_MAX_FILE_COUNT; i++) {
        if (!s_storage.objects[i].used) {
            return i + 1;
        }
    }
    return 0;
}

static void catalog_clear(uint32_t handle)
{
    if (handle > 0 && handle <= CONFIG_MTP_MAX_FILE_COUNT) {
        memset(&s_storage.objects[handle - 1], 0, sizeof(s_storage.objects[handle - 1]));
    }
}

static uint32_t catalog_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < CONFIG_MTP_MAX_FILE_COUNT; i++) {
        count += s_storage.objects[i].used ? 1 : 0;
    }
    return count;
}

typedef struct {
    bool seen[CONFIG_MTP_MAX_FILE_COUNT];
} catalog_refresh_context_t;

static void catalog_add_storage_entry(const spiffs_storage_entry_t *entry, void *context)
{
    catalog_refresh_context_t *refresh_context = context;
    uint32_t handle = catalog_find(entry->name);
    if (handle == 0) {
        handle = catalog_allocate();
    }
    if (handle == 0) {
        ESP_LOGW(TAG, "MTP catalog is full; file is hidden: %s", entry->name);
        return;
    }

    mtp_object_t *object = &s_storage.objects[handle - 1];
    object->used = true;
    strlcpy(object->name, entry->name, sizeof(object->name));
    object->size = entry->size;
    object->format = object_format_from_name(entry->name);
    refresh_context->seen[handle - 1] = true;
}

static bool catalog_refresh(void)
{
    catalog_refresh_context_t context = { 0 };
    mtp_upload_info_t upload_info;
    if (mtp_state_get_upload_info(&upload_info)) {
        context.seen[upload_info.handle - 1] = true;
    }
    if (!spiffs_storage_enumerate(catalog_add_storage_entry, &context)) {
        return false;
    }

    for (uint32_t i = 0; i < CONFIG_MTP_MAX_FILE_COUNT; i++) {
        if (!context.seen[i]) {
            memset(&s_storage.objects[i], 0, sizeof(s_storage.objects[i]));
        }
    }
    return true;
}

static void pending_upload_discard(void)
{
    mtp_upload_info_t upload_info;
    if (mtp_state_get_upload_info(&upload_info)) {
        catalog_clear(upload_info.handle);
    }
    mtp_state_discard_upload();
}

static void transfer_abort(bool remove_partial_file)
{
    if (s_storage.transfer.file != NULL) {
        spiffs_storage_close(s_storage.transfer.file);
    }
    if (remove_partial_file && s_storage.transfer.type == TRANSFER_WRITE && s_storage.transfer.name[0] != '\0') {
        spiffs_storage_remove(s_storage.transfer.name);
    }
    memset(&s_storage.transfer, 0, sizeof(s_storage.transfer));
}

static bool utf16_dataset_to_utf8(const uint8_t *dataset, size_t dataset_bytes, char *output, size_t output_size)
{
    if (dataset_bytes < 3 || output_size == 0) {
        return false;
    }

    uint8_t count = dataset[0];
    if (count < 2 || 1u + 2u * count > dataset_bytes) {
        return false;
    }

    size_t output_length = 0;
    bool terminated = false;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t first = (uint16_t)dataset[1 + 2 * i] | ((uint16_t)dataset[2 + 2 * i] << 8);
        if (first == 0) {
            terminated = true;
            break;
        }

        uint32_t codepoint = first;
        if (first >= 0xD800 && first <= 0xDBFF) {
            if (++i >= count) {
                return false;
            }
            uint16_t second = (uint16_t)dataset[1 + 2 * i] | ((uint16_t)dataset[2 + 2 * i] << 8);
            if (second < 0xDC00 || second > 0xDFFF) {
                return false;
            }
            codepoint = 0x10000u + (((uint32_t)first - 0xD800u) << 10) + ((uint32_t)second - 0xDC00u);
        } else if (first >= 0xDC00 && first <= 0xDFFF) {
            return false;
        }

        if (codepoint < 0x20 || codepoint == '/' || codepoint == '\\') {
            return false;
        }

        uint8_t encoded[4];
        size_t encoded_length;
        if (codepoint <= 0x7F) {
            encoded[0] = (uint8_t)codepoint;
            encoded_length = 1;
        } else if (codepoint <= 0x7FF) {
            encoded[0] = (uint8_t)(0xC0u | (codepoint >> 6));
            encoded[1] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
            encoded_length = 2;
        } else if (codepoint <= 0xFFFF) {
            encoded[0] = (uint8_t)(0xE0u | (codepoint >> 12));
            encoded[1] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3Fu));
            encoded[2] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
            encoded_length = 3;
        } else {
            encoded[0] = (uint8_t)(0xF0u | (codepoint >> 18));
            encoded[1] = (uint8_t)(0x80u | ((codepoint >> 12) & 0x3Fu));
            encoded[2] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3Fu));
            encoded[3] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
            encoded_length = 4;
        }

        if (output_length + encoded_length >= output_size) {
            return false;
        }
        memcpy(output + output_length, encoded, encoded_length);
        output_length += encoded_length;
    }

    output[output_length] = '\0';
    return terminated && output_length > 0 && strcmp(output, ".") != 0 && strcmp(output, "..") != 0;
}

static size_t utf8_to_utf16(const char *input, uint16_t *output, size_t output_count)
{
    size_t input_index = 0;
    size_t output_index = 0;
    while (input[input_index] != '\0' && output_index + 1 < output_count) {
        const uint8_t first = (uint8_t)input[input_index];
        uint32_t codepoint;
        size_t sequence_length;
        if (first < 0x80) {
            codepoint = first;
            sequence_length = 1;
        } else if ((first & 0xE0u) == 0xC0u) {
            codepoint = first & 0x1Fu;
            sequence_length = 2;
        } else if ((first & 0xF0u) == 0xE0u) {
            codepoint = first & 0x0Fu;
            sequence_length = 3;
        } else if ((first & 0xF8u) == 0xF0u) {
            codepoint = first & 0x07u;
            sequence_length = 4;
        } else {
            codepoint = '?';
            sequence_length = 1;
        }

        bool valid = true;
        for (size_t i = 1; i < sequence_length; i++) {
            uint8_t continuation = (uint8_t)input[input_index + i];
            if ((continuation & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3Fu);
        }
        if (!valid) {
            codepoint = '?';
            sequence_length = 1;
        }

        if (codepoint <= 0xFFFF) {
            output[output_index++] = (uint16_t)codepoint;
        } else if (output_index + 2 < output_count) {
            codepoint -= 0x10000u;
            output[output_index++] = (uint16_t)(0xD800u | (codepoint >> 10));
            output[output_index++] = (uint16_t)(0xDC00u | (codepoint & 0x3FFu));
        } else {
            break;
        }
        input_index += sequence_length;
    }
    output[output_index] = 0;
    return output_index;
}

static uint32_t collect_handles(uint32_t storage_id, uint32_t format, uint32_t parent, uint32_t *handles)
{
    if (storage_id != 0xFFFFFFFFu && storage_id != STORAGE_ID) {
        return UINT32_MAX;
    }
    if (parent != 0 && parent != 0xFFFFFFFFu) {
        return 0;
    }
    if (!catalog_refresh()) {
        return UINT32_MAX;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < CONFIG_MTP_MAX_FILE_COUNT; i++) {
        if (s_storage.objects[i].used && (format == 0 || s_storage.objects[i].format == format)) {
            if (handles != NULL) {
                handles[count] = i + 1;
            }
            count++;
        }
    }
    return count;
}

static bool read_transfer_fill(mtp_container_info_t *container)
{
    uint32_t remaining = s_storage.transfer.expected_bytes - s_storage.transfer.transferred_bytes;
    uint32_t amount = tu_min32(remaining, container->payload_bytes);
    if (amount == 0) {
        return true;
    }

    size_t read_bytes = spiffs_storage_read(s_storage.transfer.file, container->payload, amount);
    if (read_bytes != amount) {
        memset(container->payload + read_bytes, 0, amount - read_bytes);
        s_storage.transfer.failed = true;
    }
    s_storage.transfer.transferred_bytes += amount;
    return true;
}

static int32_t read_transfer_start(uint32_t handle, uint32_t offset, uint32_t length, mtp_container_info_t *container)
{
    mtp_object_t *object = catalog_get(handle);
    if (object == NULL) {
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }

    spiffs_storage_file_t *file = spiffs_storage_open(object->name, "rb");
    if (file == NULL || !spiffs_storage_seek(file, offset)) {
        if (file != NULL) {
            spiffs_storage_close(file);
        }
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }

    transfer_abort(false);
    s_storage.transfer.type = TRANSFER_READ;
    s_storage.transfer.file = file;
    s_storage.transfer.handle = handle;
    s_storage.transfer.expected_bytes = length;
    strlcpy(s_storage.transfer.name, object->name, sizeof(s_storage.transfer.name));
    container->header->len += length;
    read_transfer_fill(container);
    if (!tud_mtp_data_send(container)) {
        transfer_abort(false);
        return MTP_RESP_DEVICE_BUSY;
    }
    return 0;
}

static int32_t mtp_get_device_info(tud_mtp_cb_data_t *cb_data)
{
    mtp_container_info_t *container = &cb_data->io_container;
    mtp_container_add_cstring(container, DEVICE_INFO_MANUFACTURER);
    mtp_container_add_cstring(container, DEVICE_INFO_MODEL);
    mtp_container_add_cstring(container, DEVICE_INFO_VERSION);
    mtp_container_add_cstring(container, DEVICE_INFO_SERIAL);
    return tud_mtp_data_send(container) ? 0 : MTP_RESP_DEVICE_BUSY;
}

static int32_t mtp_open_close_session(tud_mtp_cb_data_t *cb_data)
{
    const mtp_container_command_t *command = cb_data->command_container;
    if (command->header.code == MTP_OP_OPEN_SESSION) {
        if (!mtp_state_open_session(command->params[0])) {
            return MTP_RESP_SESSION_ALREADY_OPEN;
        }
        return MTP_RESP_OK;
    }

    if (mtp_state_get_session_state() == MTP_SESSION_STATE_CLOSED) {
        return MTP_RESP_SESSION_NOT_OPEN;
    }
    transfer_abort(true);
    pending_upload_discard();
    mtp_state_close_session();
    return MTP_RESP_OK;
}

static int32_t mtp_get_storage_ids(tud_mtp_cb_data_t *cb_data)
{
    const uint32_t storage_ids[] = { STORAGE_ID };
    mtp_container_add_auint32(&cb_data->io_container, TU_ARRAY_SIZE(storage_ids), storage_ids);
    return tud_mtp_data_send(&cb_data->io_container) ? 0 : MTP_RESP_DEVICE_BUSY;
}

static int32_t mtp_get_storage_info(tud_mtp_cb_data_t *cb_data)
{
    if (cb_data->command_container->params[0] != STORAGE_ID) {
        return MTP_RESP_INVALID_STORAGE_ID;
    }

    size_t total_bytes;
    size_t used_bytes;
    if (!spiffs_storage_get_capacity(&total_bytes, &used_bytes) || !catalog_refresh()) {
        return MTP_RESP_STORE_NOT_AVAILABLE;
    }

    const mtp_storage_info_t storage_info = {
        .storage_type = MTP_STORAGE_TYPE_FIXED_RAM,
        .filesystem_type = MTP_FILESYSTEM_TYPE_GENERIC_FLAT,
        .access_capability = MTP_ACCESS_CAPABILITY_READ_WRITE,
        .max_capacity_in_bytes = total_bytes,
        .free_space_in_bytes = total_bytes > used_bytes ? total_bytes - used_bytes : 0,
        .free_space_in_objects = CONFIG_MTP_MAX_FILE_COUNT - catalog_count(),
        .storage_description = {
            .count = STORAGE_DESCRIPTION_LEN,
            .utf16 = STORAGE_DESCRIPTION,
        },
        .volume_identifier = {
            .count = VOLUME_IDENTIFIER_LEN,
            .utf16 = VOLUME_IDENTIFIER,
        },
    };
    mtp_container_add_raw(&cb_data->io_container, &storage_info, sizeof(storage_info));
    return tud_mtp_data_send(&cb_data->io_container) ? 0 : MTP_RESP_DEVICE_BUSY;
}

static int32_t mtp_get_device_property(tud_mtp_cb_data_t *cb_data)
{
    const mtp_container_command_t *command = cb_data->command_container;
    mtp_container_info_t *container = &cb_data->io_container;
    uint16_t property = (uint16_t)command->params[0];
    if (property != MTP_DEV_PROP_DEVICE_FRIENDLY_NAME) {
        return MTP_RESP_DEVICE_PROP_NOT_SUPPORTED;
    }

    if (command->header.code == MTP_OP_GET_DEVICE_PROP_DESC) {
        const mtp_device_prop_desc_header_t descriptor = {
            .device_property_code = property,
            .datatype = MTP_DATA_TYPE_STR,
            .get_set = MTP_MODE_GET,
        };
        mtp_container_add_raw(container, &descriptor, sizeof(descriptor));
        mtp_container_add_cstring(container, DEVICE_FRIENDLY_NAME);
        mtp_container_add_cstring(container, DEVICE_FRIENDLY_NAME);
        mtp_container_add_uint8(container, 0);
    } else {
        mtp_container_add_cstring(container, DEVICE_FRIENDLY_NAME);
    }
    return tud_mtp_data_send(container) ? 0 : MTP_RESP_DEVICE_BUSY;
}

static int32_t mtp_get_num_objects(tud_mtp_cb_data_t *cb_data)
{
    const mtp_container_command_t *command = cb_data->command_container;
    uint32_t count = collect_handles(command->params[0], command->params[1], command->params[2], NULL);
    if (count == UINT32_MAX) {
        return MTP_RESP_INVALID_STORAGE_ID;
    }
    mtp_container_add_uint32(&cb_data->io_container, count);
    return MTP_RESP_OK;
}

static int32_t mtp_get_object_handles(tud_mtp_cb_data_t *cb_data)
{
    const mtp_container_command_t *command = cb_data->command_container;
    uint32_t handles[CONFIG_MTP_MAX_FILE_COUNT];
    uint32_t count = collect_handles(command->params[0], command->params[1], command->params[2], handles);
    if (count == UINT32_MAX) {
        return MTP_RESP_INVALID_STORAGE_ID;
    }
    mtp_container_add_auint32(&cb_data->io_container, count, handles);
    return tud_mtp_data_send(&cb_data->io_container) ? 0 : MTP_RESP_DEVICE_BUSY;
}

static int32_t mtp_get_object_info(tud_mtp_cb_data_t *cb_data)
{
    uint32_t handle = cb_data->command_container->params[0];
    mtp_object_t *object = catalog_get(handle);
    if (object == NULL) {
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }

    uint32_t file_size;
    if (!spiffs_storage_get_file_size(object->name, &file_size)) {
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }
    object->size = file_size;

    const mtp_object_info_header_t object_info = {
        .storage_id = STORAGE_ID,
        .object_format = object->format,
        .protection_status = MTP_PROTECTION_STATUS_NO_PROTECTION,
        .object_compressed_size = object->size,
        .thumb_format = MTP_OBJ_FORMAT_UNDEFINED,
        .thumb_compressed_size = 0,
        .thumb_pix_width = 0,
        .thumb_pix_height = 0,
        .image_pix_width = 0,
        .image_pix_height = 0,
        .image_bit_depth = 0,
        .parent_object = 0,
        .association_type = MTP_ASSOCIATION_UNDEFINED,
        .association_desc = 0,
        .sequence_number = 0,
    };
    uint16_t filename[CONFIG_SPIFFS_OBJ_NAME_LEN];
    utf8_to_utf16(object->name, filename, TU_ARRAY_SIZE(filename));
    mtp_container_add_raw(&cb_data->io_container, &object_info, sizeof(object_info));
    mtp_container_add_string(&cb_data->io_container, filename);
    mtp_container_add_cstring(&cb_data->io_container, "");
    mtp_container_add_cstring(&cb_data->io_container, "");
    mtp_container_add_cstring(&cb_data->io_container, "");
    return tud_mtp_data_send(&cb_data->io_container) ? 0 : MTP_RESP_DEVICE_BUSY;
}

static int32_t mtp_get_object(tud_mtp_cb_data_t *cb_data)
{
    uint32_t handle = cb_data->command_container->params[0];
    mtp_object_t *object = catalog_get(handle);
    if (object == NULL) {
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }
    if (cb_data->phase == MTP_PHASE_COMMAND) {
        return read_transfer_start(handle, 0, object->size, &cb_data->io_container);
    }
    read_transfer_fill(&cb_data->io_container);
    return tud_mtp_data_send(&cb_data->io_container) ? 0 : -1;
}

static int32_t mtp_get_partial_object(tud_mtp_cb_data_t *cb_data)
{
    const mtp_container_command_t *command = cb_data->command_container;
    mtp_object_t *object = catalog_get(command->params[0]);
    if (object == NULL) {
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }

    uint32_t offset = command->params[1];
    uint32_t available = offset < object->size ? object->size - offset : 0;
    uint32_t length = tu_min32(available, command->params[2]);
    if (cb_data->phase == MTP_PHASE_COMMAND) {
        return read_transfer_start(command->params[0], offset, length, &cb_data->io_container);
    }
    read_transfer_fill(&cb_data->io_container);
    return tud_mtp_data_send(&cb_data->io_container) ? 0 : -1;
}

static void set_send_info_error(uint16_t response)
{
    mtp_state_set_deferred_response(response);
    pending_upload_discard();
}

static void parse_send_object_info(tud_mtp_cb_data_t *cb_data)
{
    const mtp_container_command_t *command = cb_data->command_container;
    mtp_container_info_t *container = &cb_data->io_container;
    if (container->payload_bytes < sizeof(mtp_object_info_header_t) + 3) {
        set_send_info_error(MTP_RESP_INVALID_DATASET);
        return;
    }

    mtp_object_info_header_t object_info;
    memcpy(&object_info, container->payload, sizeof(object_info));
    if ((command->params[0] != 0xFFFFFFFFu && command->params[0] != STORAGE_ID) || (object_info.storage_id != 0 && object_info.storage_id != STORAGE_ID)) {
        set_send_info_error(MTP_RESP_INVALID_STORAGE_ID);
        return;
    }
    if ((command->params[1] != 0 && command->params[1] != 0xFFFFFFFFu) || (object_info.parent_object != 0 && object_info.parent_object != 0xFFFFFFFFu)) {
        set_send_info_error(MTP_RESP_INVALID_PARENT_OBJECT);
        return;
    }
    if (object_info.object_format == MTP_OBJ_FORMAT_ASSOCIATION) {
        set_send_info_error(MTP_RESP_INVALID_OBJECT_FORMAT_CODE);
        return;
    }

    char filename[MTP_FILENAME_MAX + 1];
    const uint8_t *filename_dataset = container->payload + sizeof(object_info);
    size_t filename_dataset_bytes = container->payload_bytes - sizeof(object_info);
    if (!utf16_dataset_to_utf8(filename_dataset, filename_dataset_bytes, filename, sizeof(filename))) {
        set_send_info_error(MTP_RESP_INVALID_DATASET);
        return;
    }
    if (!catalog_refresh()) {
        set_send_info_error(MTP_RESP_STORE_NOT_AVAILABLE);
        return;
    }
    if (catalog_find(filename) != 0) {
        set_send_info_error(MTP_RESP_ACCESS_DENIED);
        return;
    }

    size_t total_bytes;
    size_t used_bytes;
    if (!spiffs_storage_get_capacity(&total_bytes, &used_bytes)) {
        set_send_info_error(MTP_RESP_STORE_NOT_AVAILABLE);
        return;
    }
    size_t free_bytes = total_bytes > used_bytes ? total_bytes - used_bytes : 0;
    if (object_info.object_compressed_size > free_bytes) {
        set_send_info_error(MTP_RESP_STORE_FULL);
        return;
    }

    uint32_t handle = catalog_allocate();
    if (handle == 0) {
        set_send_info_error(MTP_RESP_STORE_FULL);
        return;
    }

    mtp_object_t *object = &s_storage.objects[handle - 1];
    object->used = true;
    strlcpy(object->name, filename, sizeof(object->name));
    object->size = object_info.object_compressed_size;
    object->format = object_info.object_format == 0 ? object_format_from_name(filename) : object_info.object_format;
    const mtp_upload_info_t upload_info = {
        .handle = handle,
        .parent = 0,
        .size = object_info.object_compressed_size,
    };
    if (!mtp_state_set_upload_info(&upload_info)) {
        set_send_info_error(MTP_RESP_GENERAL_ERROR);
    }
}

static int32_t mtp_send_object_info(tud_mtp_cb_data_t *cb_data)
{
    if (mtp_state_get_session_state() == MTP_SESSION_STATE_CLOSED) {
        return MTP_RESP_SESSION_NOT_OPEN;
    }
    if (cb_data->phase == MTP_PHASE_COMMAND) {
        transfer_abort(true);
        pending_upload_discard();
        mtp_state_begin_upload_info();
        if (!tud_mtp_data_receive(&cb_data->io_container)) {
            mtp_state_discard_upload();
            return MTP_RESP_DEVICE_BUSY;
        }
        return 0;
    }

    if (mtp_state_get_upload_state() == MTP_UPLOAD_STATE_INFO_RECEIVING) {
        parse_send_object_info(cb_data);
    }
    if (cb_data->total_xferred_bytes < cb_data->io_container.header->len) {
        return tud_mtp_data_receive(&cb_data->io_container) ? 0 : -1;
    }
    return 0;
}

static int32_t mtp_send_object(tud_mtp_cb_data_t *cb_data)
{
    if (mtp_state_get_session_state() == MTP_SESSION_STATE_CLOSED) {
        return MTP_RESP_SESSION_NOT_OPEN;
    }
    mtp_upload_info_t upload_info;
    if (!mtp_state_get_upload_info(&upload_info)) {
        return MTP_RESP_NO_VALID_OBJECTINFO;
    }

    if (cb_data->phase == MTP_PHASE_COMMAND) {
        if (mtp_state_get_upload_state() != MTP_UPLOAD_STATE_INFO_READY) {
            return MTP_RESP_DEVICE_BUSY;
        }
        mtp_object_t *object = catalog_get(upload_info.handle);
        if (object == NULL) {
            return MTP_RESP_GENERAL_ERROR;
        }

        strlcpy(s_storage.transfer.name, object->name, sizeof(s_storage.transfer.name));
        s_storage.transfer.file = spiffs_storage_open(s_storage.transfer.name, "wb");
        if (s_storage.transfer.file == NULL) {
            return errno == ENOSPC ? MTP_RESP_STORE_FULL : MTP_RESP_ACCESS_DENIED;
        }
        s_storage.transfer.type = TRANSFER_WRITE;
        s_storage.transfer.handle = upload_info.handle;
        s_storage.transfer.expected_bytes = upload_info.size;
        s_storage.transfer.transferred_bytes = 0;
        s_storage.transfer.failed = false;
        mtp_state_set_deferred_response(MTP_RESP_OK);
        cb_data->io_container.header->len += s_storage.transfer.expected_bytes;
        if (!mtp_state_begin_upload_data()) {
            transfer_abort(true);
            return MTP_RESP_GENERAL_ERROR;
        }
        if (!tud_mtp_data_receive(&cb_data->io_container)) {
            transfer_abort(true);
            pending_upload_discard();
            return MTP_RESP_DEVICE_BUSY;
        }
        return 0;
    }

    uint32_t remaining = s_storage.transfer.expected_bytes - s_storage.transfer.transferred_bytes;
    uint32_t amount = tu_min32(remaining, cb_data->io_container.payload_bytes);
    if (amount > 0) {
        size_t written = spiffs_storage_write(s_storage.transfer.file, cb_data->io_container.payload, amount);
        if (written != amount) {
            s_storage.transfer.failed = true;
            mtp_state_set_deferred_response(MTP_RESP_STORE_FULL);
        }
        s_storage.transfer.transferred_bytes += amount;
    }
    if (cb_data->total_xferred_bytes - sizeof(mtp_container_header_t) < s_storage.transfer.expected_bytes) {
        return tud_mtp_data_receive(&cb_data->io_container) ? 0 : -1;
    }
    return 0;
}

static int32_t mtp_delete_object(tud_mtp_cb_data_t *cb_data)
{
    if (mtp_state_get_session_state() == MTP_SESSION_STATE_CLOSED) {
        return MTP_RESP_SESSION_NOT_OPEN;
    }

    uint32_t handle = cb_data->command_container->params[0];
    mtp_object_t *object = catalog_get(handle);
    if (object == NULL) {
        return MTP_RESP_INVALID_OBJECT_HANDLE;
    }

    char filename[MTP_FILENAME_MAX + 1];
    strlcpy(filename, object->name, sizeof(filename));
    if (!spiffs_storage_remove(filename)) {
        return MTP_RESP_OBJECT_WRITE_PROTECTED;
    }
    catalog_clear(handle);
    mtp_upload_info_t upload_info;
    if (mtp_state_get_upload_info(&upload_info) && upload_info.handle == handle) {
        mtp_state_discard_upload();
    }
    ESP_LOGI(TAG, "Deleted %s", filename);
    return MTP_RESP_OK;
}

static mtp_operation_handler_t find_operation_handler(uint16_t operation)
{
    for (size_t i = 0; i < TU_ARRAY_SIZE(s_operation_handlers); i++) {
        if (s_operation_handlers[i].operation == operation) {
            return s_operation_handlers[i].handler;
        }
    }
    return NULL;
}

static int32_t dispatch_operation(tud_mtp_cb_data_t *cb_data)
{
    mtp_operation_handler_t handler = find_operation_handler(cb_data->command_container->header.code);
    return handler == NULL ? MTP_RESP_OPERATION_NOT_SUPPORTED : handler(cb_data);
}

static int32_t dispatch_and_respond(tud_mtp_cb_data_t *cb_data)
{
    int32_t response = dispatch_operation(cb_data);
    if (response > MTP_RESP_UNDEFINED) {
        cb_data->io_container.header->code = (uint16_t)response;
        tud_mtp_response_send(&cb_data->io_container);
    }
    return response;
}

static uint16_t finish_upload(tud_mtp_cb_data_t *cb_data)
{
    bool success = cb_data->xfer_result == XFER_RESULT_SUCCESS && !s_storage.transfer.failed && s_storage.transfer.transferred_bytes == s_storage.transfer.expected_bytes;
    if (!spiffs_storage_close(s_storage.transfer.file)) {
        success = false;
    }
    s_storage.transfer.file = NULL;

    uint32_t handle = s_storage.transfer.handle;
    char filename[MTP_FILENAME_MAX + 1];
    strlcpy(filename, s_storage.transfer.name, sizeof(filename));
    memset(&s_storage.transfer, 0, sizeof(s_storage.transfer));
    if (!success) {
        spiffs_storage_remove(filename);
        pending_upload_discard();
        uint16_t response = mtp_state_get_deferred_response();
        return response == MTP_RESP_OK ? MTP_RESP_INCOMPLETE_TRANSFER : response;
    }

    mtp_object_t *object = catalog_get(handle);
    uint32_t file_size;
    if (object == NULL || !spiffs_storage_get_file_size(filename, &file_size)) {
        spiffs_storage_remove(filename);
        pending_upload_discard();
        return MTP_RESP_GENERAL_ERROR;
    }
    object->size = file_size;
    mtp_state_queue_object_added(handle, cb_data->command_container->header.transaction_id);
    mtp_state_discard_upload();
    ESP_LOGI(TAG, "Stored %s (%lu bytes)", object->name, (unsigned long)object->size);
    return MTP_RESP_OK;
}

esp_err_t mtp_storage_init(void)
{
    esp_err_t error = spiffs_storage_init();
    if (error != ESP_OK) {
        return error;
    }

    if (!catalog_refresh()) {
        spiffs_storage_deinit();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MTP catalog contains %lu files", (unsigned long)catalog_count());
    return ESP_OK;
}

void mtp_storage_reset(void)
{
    transfer_abort(true);
    pending_upload_discard();
    mtp_state_reset();
}

bool tud_mtp_request_cancel_cb(tud_mtp_request_cb_data_t *cb_data)
{
    (void)cb_data;
    transfer_abort(true);
    pending_upload_discard();
    return true;
}

bool tud_mtp_request_device_reset_cb(tud_mtp_request_cb_data_t *cb_data)
{
    (void)cb_data;
    mtp_storage_reset();
    return true;
}

int32_t tud_mtp_request_get_extended_event_cb(tud_mtp_request_cb_data_t *cb_data)
{
    (void)cb_data;
    return -1;
}

int32_t tud_mtp_request_get_device_status_cb(tud_mtp_request_cb_data_t *cb_data)
{
    const uint16_t status[] = { 4, MTP_RESP_OK };
    memcpy(cb_data->buf, status, sizeof(status));
    return sizeof(status);
}

int32_t tud_mtp_command_received_cb(tud_mtp_cb_data_t *cb_data)
{
    return dispatch_and_respond(cb_data);
}

int32_t tud_mtp_data_xfer_cb(tud_mtp_cb_data_t *cb_data)
{
    int32_t response = dispatch_and_respond(cb_data);
    return response < 0 ? response : 0;
}

int32_t tud_mtp_data_complete_cb(tud_mtp_cb_data_t *cb_data)
{
    mtp_container_info_t *response = &cb_data->io_container;
    uint16_t response_code = cb_data->xfer_result == XFER_RESULT_SUCCESS ? MTP_RESP_OK : MTP_RESP_GENERAL_ERROR;
    switch (cb_data->command_container->header.code) {
    case MTP_OP_SEND_OBJECT_INFO:
        response_code = cb_data->xfer_result == XFER_RESULT_SUCCESS ? mtp_state_get_deferred_response() : MTP_RESP_GENERAL_ERROR;
        mtp_upload_info_t upload_info;
        if (response_code == MTP_RESP_OK && mtp_state_get_upload_info(&upload_info)) {
            mtp_container_add_uint32(response, STORAGE_ID);
            mtp_container_add_uint32(response, upload_info.parent);
            mtp_container_add_uint32(response, upload_info.handle);
        } else if (response_code == MTP_RESP_OK) {
            response_code = MTP_RESP_INVALID_DATASET;
        }
        if (response_code != MTP_RESP_OK) {
            pending_upload_discard();
        }
        break;
    case MTP_OP_SEND_OBJECT:
        response_code = finish_upload(cb_data);
        break;
    case MTP_OP_GET_OBJECT:
        if (s_storage.transfer.failed) {
            response_code = MTP_RESP_GENERAL_ERROR;
        }
        transfer_abort(false);
        break;
    case MTP_OP_GET_PARTIAL_OBJECT:
        if (s_storage.transfer.failed) {
            response_code = MTP_RESP_GENERAL_ERROR;
        } else {
            mtp_container_add_uint32(response, cb_data->total_xferred_bytes - sizeof(mtp_container_header_t));
        }
        transfer_abort(false);
        break;
    default:
        break;
    }

    response->header->code = response_code;
    tud_mtp_response_send(response);
    return 0;
}

int32_t tud_mtp_response_complete_cb(tud_mtp_cb_data_t *cb_data)
{
    (void)cb_data;
    mtp_object_added_event_t pending_event;
    if (mtp_state_get_pending_object_added(&pending_event)) {
        mtp_event_t event = {
            .code = MTP_EVENT_OBJECT_ADDED,
            .session_id = mtp_state_get_session_id(),
            .transaction_id = pending_event.transaction_id,
            .params = { pending_event.handle, 0, 0 },
        };
        if (tud_mtp_event_send(&event)) {
            mtp_state_complete_object_added();
        }
    }
    return 0;
}
