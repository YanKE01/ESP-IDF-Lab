idf_component_register(
    SRCS "early_gpio_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system log soc
)
