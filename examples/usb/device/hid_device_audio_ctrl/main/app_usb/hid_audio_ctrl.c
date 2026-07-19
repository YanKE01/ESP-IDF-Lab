#include "hid_audio_ctrl.h"

#include "class/hid/hid_device.h"
#include "tusb.h"

/**
 * @brief Handles an HID GET_REPORT request received on the control endpoint.
 *
 * This example does not support GET_REPORT, so returning zero causes TinyUSB
 * to stall the control request.
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

/**
 * @brief Handles an HID SET_REPORT request or data received on an OUT endpoint.
 *
 * This Consumer Control interface has no OUT endpoint and ignores SET_REPORT.
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool hid_device_audio_ctrl(void)
{
    if (!tud_hid_ready()) {
        return false;
    }

    uint16_t usage = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
    return tud_hid_report(0, &usage, sizeof(usage));
}

/**
 * @brief Sends the release report after a Consumer Control report completes.
 *
 * The interface has no Report ID, so the two report bytes contain the Consumer
 * Control usage. A zero usage releases the previously pressed media key.
 */
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance;

    if (len == sizeof(uint16_t) && (report[0] != 0 || report[1] != 0)) {
        uint16_t usage = 0;
        tud_hid_report(0, &usage, sizeof(usage));
    }
}
