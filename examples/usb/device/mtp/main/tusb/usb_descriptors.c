#include <string.h>

#include "tusb.h"

enum {
    ITF_NUM_MTP,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MTP,
};

#define EPNUM_MTP_EVENT 0x81
#define EPNUM_MTP_OUT 0x02
#define EPNUM_MTP_IN 0x82
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MTP_DESC_LEN)
#define MTP_FS_EVENT_INTERVAL 1
#define MTP_HS_EVENT_INTERVAL 4

static tusb_desc_device_t const s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

static uint8_t const s_fs_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MTP_DESCRIPTOR(ITF_NUM_MTP, STRID_MTP, EPNUM_MTP_EVENT, 64, MTP_FS_EVENT_INTERVAL, EPNUM_MTP_OUT, EPNUM_MTP_IN, 64),
};

#if TUD_OPT_HIGH_SPEED
static uint8_t const s_hs_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MTP_DESCRIPTOR(ITF_NUM_MTP, STRID_MTP, EPNUM_MTP_EVENT, 64, MTP_HS_EVENT_INTERVAL, EPNUM_MTP_OUT, EPNUM_MTP_IN, 512),
};

static tusb_desc_device_qualifier_t const s_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0x00,
};

static uint8_t s_other_speed_configuration[CONFIG_TOTAL_LEN];
#endif

static char const *s_string_descriptors[] = {
    [STRID_LANGID] = NULL,
    [STRID_MANUFACTURER] = "TinyUSB",
    [STRID_PRODUCT] = "TinyUSB MTP Device",
    [STRID_SERIAL] = "123456",
    [STRID_MTP] = "TinyUSB MTP",
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_device_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
#if TUD_OPT_HIGH_SPEED
    return tud_speed_get() == TUSB_SPEED_HIGH ? s_hs_configuration_descriptor : s_fs_configuration_descriptor;
#else
    return s_fs_configuration_descriptor;
#endif
}

#if TUD_OPT_HIGH_SPEED
uint8_t const *tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const *)&s_device_qualifier;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    uint8_t const *source = tud_speed_get() == TUSB_SPEED_HIGH ? s_fs_configuration_descriptor : s_hs_configuration_descriptor;
    memcpy(s_other_speed_configuration, source, CONFIG_TOTAL_LEN);
    s_other_speed_configuration[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return s_other_speed_configuration;
}
#endif

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    static uint16_t descriptor[32];
    uint8_t character_count;

    if (index == STRID_LANGID) {
        descriptor[1] = 0x0409;
        character_count = 1;
    } else {
        if (index >= TU_ARRAY_SIZE(s_string_descriptors) || s_string_descriptors[index] == NULL) {
            return NULL;
        }

        char const *string = s_string_descriptors[index];
        character_count = (uint8_t)strlen(string);
        if (character_count > TU_ARRAY_SIZE(descriptor) - 1) {
            character_count = TU_ARRAY_SIZE(descriptor) - 1;
        }

        for (uint8_t i = 0; i < character_count; i++) {
            descriptor[1 + i] = string[i];
        }
    }

    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * character_count + 2));
    return descriptor;
}
