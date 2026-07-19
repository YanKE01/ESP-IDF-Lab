#include "hid_keyboard.h"

#include "class/hid/hid_device.h"
#include "tusb.h"

/**
 * @brief Handles an HID GET_REPORT request received on the control endpoint.
 *
 * The application should fill buffer with the requested report and return its
 * actual length. This example does not support GET_REPORT, so returning zero
 * causes TinyUSB to stall the control request.
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
 * For a keyboard with only an IN endpoint, the host usually sends keyboard LED
 * states such as Num Lock, Caps Lock, and Scroll Lock over the control endpoint.
 * This example ignores these output reports.
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

/**
 * @brief Sends a report that presses the left Windows key once.
 *
 * tud_hid_report_complete_cb() submits the release report after the press report
 * has completed.
 */
void keyboard_win_test(void)
{
    if (!tud_hid_ready()) {
        return;
    }

    // A single keyboard interface does not need a Report ID, so report_id is 0.
    tud_hid_keyboard_report(0, KEYBOARD_MODIFIER_LEFTGUI, NULL);
}

/**
 * @brief Called by TinyUSB after an HID IN report is sent successfully.
 *
 * This interface has no Report ID, so report[0] is the keyboard modifier field.
 * After the press report completes, an all-zero report releases the key. When
 * that release report completes, report[0] is zero and no new report is sent.
 */
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance;

    // Without a Report ID, report[0] is the modifier field.
    if (len > 0 && report[0] != 0) {
        tud_hid_keyboard_report(0, 0, NULL);
    }
}
