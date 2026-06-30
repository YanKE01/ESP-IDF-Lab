#include <stdio.h>

#include "esp_heap_caps.h"

#include "download_speed.h"
#include "net_connect.h"

void app_main(void)
{
    printf("app main, DRAM: %d KB, Psram: %d KB\n",
           heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024,
           heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM) / 1024);

    net_connect();
    download_speed_start();
}
