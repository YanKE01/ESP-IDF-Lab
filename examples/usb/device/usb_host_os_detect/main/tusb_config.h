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

#define USB_VID                      0x303A
#define USB_PID                      0x4002

//------------- CLASS -------------//
// Stage 1: only a neutral vendor interface is exposed. Its sole purpose is to
// get the device enumerated so we can observe how the host probes it and infer
// the OS. The Windows-MSC / Linux-CDC personalities come in a later stage.
#define CFG_TUD_VENDOR               1   /* neutral probe interface */
#define CFG_TUD_CDC                  0
#define CFG_TUD_MSC                  0

#define VENDOR_BUF_SIZE              (CONFIG_USB_HS ? 512 : 64)
#define CFG_TUD_VENDOR_RX_BUFSIZE    (VENDOR_BUF_SIZE * 4)
#define CFG_TUD_VENDOR_TX_BUFSIZE    (VENDOR_BUF_SIZE * 4)
#ifndef CFG_TUD_VENDOR_EPSIZE
#define CFG_TUD_VENDOR_EPSIZE        VENDOR_BUF_SIZE
#endif

#ifdef __cplusplus
}
#endif
