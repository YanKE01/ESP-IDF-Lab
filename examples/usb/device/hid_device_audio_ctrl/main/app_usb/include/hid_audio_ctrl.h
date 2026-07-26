#pragma once

#include <stdbool.h>

/**
 * @brief Sends a Consumer Control report that decreases the host volume.
 *
 * @return true if the report was submitted, otherwise false.
 */
bool hid_device_audio_ctrl(void);
