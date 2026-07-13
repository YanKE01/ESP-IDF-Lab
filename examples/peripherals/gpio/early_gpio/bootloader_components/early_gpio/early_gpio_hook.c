/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdint.h>

#include "sdkconfig.h"

#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This example only supports ESP32-S3"
#endif

#if CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO >= 22 && CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO <= 25
#error "GPIO22 through GPIO25 are not valid ESP32-S3 GPIOs"
#endif

/*
 * This symbol forces the linker to include this component even though the
 * bootloader hook declaration itself is weak.
 */
void bootloader_hooks_include(void)
{
}

/*
 * This is the first supported user hook called after the ROM has loaded the
 * second-stage bootloader. Hardware, flash cache, and BSS are not initialized,
 * so this path only performs direct, inline register operations.
 */
void bootloader_before_init(void)
{
    const uint32_t gpio_num = CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO;

    /* Set the latch before enabling output to avoid an opposite-level glitch. */
#if CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO < 32
#if CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL
    GPIO.out_w1ts = 1U << CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO;
#else
    GPIO.out_w1tc = 1U << CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO;
#endif
#else
#if CONFIG_EXAMPLE_EARLY_SIGNAL_LEVEL
    GPIO.out1_w1ts.val = 1U << (CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO - 32);
#else
    GPIO.out1_w1tc.val = 1U << (CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO - 32);
#endif
#endif

    /* Route the software GPIO latch to the pad as a push-pull output. */
    GPIO.func_out_sel_cfg[gpio_num].func_sel = SIG_GPIO_OUT_IDX;
    GPIO.func_out_sel_cfg[gpio_num].inv_sel = 0;
    GPIO.func_out_sel_cfg[gpio_num].oen_sel = 1;
    GPIO.func_out_sel_cfg[gpio_num].oen_inv_sel = 0;
    GPIO.pin[gpio_num].pad_driver = 0;
    PIN_FUNC_SELECT(IO_MUX_GPIO0_REG + (gpio_num * 4), PIN_FUNC_GPIO);

    /* The customer-visible transition occurs on this final register write. */
#if CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO < 32
    GPIO.enable_w1ts = 1U << CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO;
#else
    GPIO.enable1_w1ts.val = 1U << (CONFIG_EXAMPLE_EARLY_SIGNAL_GPIO - 32);
#endif
}
