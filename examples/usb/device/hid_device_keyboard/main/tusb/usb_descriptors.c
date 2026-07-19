#include <string.h>

#include "tusb.h"

enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_HID,
};

#define EPNUM_HID_IN 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define HID_POLLING_INTERVAL (TUD_OPT_HIGH_SPEED ? 4 : 1)

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

static uint8_t const s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

static uint8_t const s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_descriptor), EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, HID_POLLING_INTERVAL),
};

#if TUD_OPT_HIGH_SPEED
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
    [STRID_PRODUCT] = "TinyUSB HID Keyboard",
    [STRID_SERIAL] = "123456",
    [STRID_HID] = "TinyUSB HID Keyboard Interface",
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_device_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_configuration_descriptor;
}

#if TUD_OPT_HIGH_SPEED
uint8_t const *tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const *)&s_device_qualifier;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;

    memcpy(s_other_speed_configuration, s_configuration_descriptor, CONFIG_TOTAL_LEN);
    s_other_speed_configuration[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return s_other_speed_configuration;
}
#endif

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

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
