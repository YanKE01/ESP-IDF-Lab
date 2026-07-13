/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>

#include "esp_log_timestamp.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "soc/gpio_struct.h"

static inline __attribute__((always_inline)) void deassert_early_signal(void)
{
    /*
     * The bootloader already configured the pad as a push-pull GPIO output.
     * Only one direct latch write is needed here. With the default active-high
     * configuration, this is the customer-visible falling edge.
     */
#if CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO < 32
#if CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL
    GPIO.out_w1tc = 1U << CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO;
#else
    GPIO.out_w1ts = 1U << CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO;
#endif
#else
#if CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL
    GPIO.out1_w1tc.val = 1U << (CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO - 32);
#else
    GPIO.out1_w1ts.val = 1U << (CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO - 32);
#endif
#endif
}

void app_main(void)
{
    /* Keep this as the first operation: it terminates the timing pulse. */
    deassert_early_signal();

    const uint32_t app_main_time_ms = esp_log_early_timestamp();

    printf("APP_MAIN_TIME_MS=%" PRIu32 "\n", app_main_time_ms);
    printf("EARLY_SIGNAL_GPIO=%d\n", CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO);
    printf("EARLY_SIGNAL_LEVEL=%d\n", CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL);
    printf("APP_MAIN_SIGNAL_LEVEL=%d\n", !CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL);
    printf("RESET_REASON=%d\n", (int)esp_reset_reason());
    printf("POWER_ON_RESET=%s\n", esp_reset_reason() == ESP_RST_POWERON ? "yes" : "no");
    fflush(stdout);
}
