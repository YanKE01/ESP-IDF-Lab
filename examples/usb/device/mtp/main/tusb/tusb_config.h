/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

#ifdef CONFIG_TINYUSB_RHPORT_HS
#   define CFG_TUSB_RHPORT1_MODE    OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED
#   define CONFIG_USB_HS            1
#else
#   define CFG_TUSB_RHPORT0_MODE    OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED
#   define CONFIG_USB_HS            0
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_FREERTOS
#endif

#ifndef ESP_PLATFORM
#define ESP_PLATFORM 1
#endif

// Espressif IDF requires "freertos/" prefix in include path
#if TU_CHECK_MCU(OPT_MCU_ESP32S2, OPT_MCU_ESP32S3, OPT_MCU_ESP32P4)
#define CFG_TUSB_OS_INC_PATH    freertos/
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

#define USB_VID                      0xCAFE
#define USB_PID                      0x4020

//------------- CLASS -------------//
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0
#define CFG_TUD_MTP               1

// MTP endpoint software buffers
#define CFG_TUD_MTP_EP_BUFSIZE            512
#define CFG_TUD_MTP_EP_CONTROL_BUFSIZE    16

//------------- MTP DEVICE INFO -------------//
#define CFG_TUD_MTP_DEVICEINFO_EXTENSIONS    "microsoft.com: 1.0; "
#define CFG_TUD_MTP_DEVICEINFO_SUPPORTED_OPERATIONS \
    MTP_OP_GET_DEVICE_INFO, \
    MTP_OP_OPEN_SESSION, \
    MTP_OP_CLOSE_SESSION, \
    MTP_OP_GET_STORAGE_IDS, \
    MTP_OP_GET_STORAGE_INFO, \
    MTP_OP_GET_NUM_OBJECTS, \
    MTP_OP_GET_OBJECT_HANDLES, \
    MTP_OP_GET_OBJECT_INFO, \
    MTP_OP_GET_OBJECT, \
    MTP_OP_GET_PARTIAL_OBJECT, \
    MTP_OP_DELETE_OBJECT, \
    MTP_OP_SEND_OBJECT_INFO, \
    MTP_OP_SEND_OBJECT, \
    MTP_OP_GET_DEVICE_PROP_DESC, \
    MTP_OP_GET_DEVICE_PROP_VALUE

#define CFG_TUD_MTP_DEVICEINFO_SUPPORTED_EVENTS \
    MTP_EVENT_OBJECT_ADDED

#define CFG_TUD_MTP_DEVICEINFO_SUPPORTED_DEVICE_PROPERTIES \
    MTP_DEV_PROP_DEVICE_FRIENDLY_NAME

#define CFG_TUD_MTP_DEVICEINFO_CAPTURE_FORMATS \
    MTP_OBJ_FORMAT_UNDEFINED, \
    MTP_OBJ_FORMAT_TEXT, \
    MTP_OBJ_FORMAT_HTML, \
    MTP_OBJ_FORMAT_EXIF_JPEG, \
    MTP_OBJ_FORMAT_PNG, \
    MTP_OBJ_FORMAT_GIF, \
    MTP_OBJ_FORMAT_BMP, \
    MTP_OBJ_FORMAT_MP3, \
    MTP_OBJ_FORMAT_WAV, \
    MTP_OBJ_FORMAT_MP4

#define CFG_TUD_MTP_DEVICEINFO_PLAYBACK_FORMATS \
    CFG_TUD_MTP_DEVICEINFO_CAPTURE_FORMATS

#ifdef __cplusplus
}
#endif
