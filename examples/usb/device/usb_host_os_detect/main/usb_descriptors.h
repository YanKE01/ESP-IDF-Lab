#pragma once

#include <stdbool.h>
#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True after the host fetched our configuration descriptor, i.e. it is seriously
 * enumerating us. Both Windows and Linux do this; it is the "enumeration has
 * started" marker we time the SET_CONFIGURATION decision from. */
bool usb_descriptors_config_requested(void);

#ifdef __cplusplus
}
#endif
