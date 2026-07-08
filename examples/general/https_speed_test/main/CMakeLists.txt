idf_component_register(
    SRCS
        "main.c"
        "net_connect.c"
        "download_speed.c"
    INCLUDE_DIRS "."
    REQUIRES protocol_examples_common esp_event esp_netif nvs_flash esp_http_client esp_timer esp_psram
    PRIV_REQUIRES mbedtls
)
