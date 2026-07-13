# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0

idf_component_register(
    SRCS "early_gpio_hook.c"
    REQUIRES soc
)
