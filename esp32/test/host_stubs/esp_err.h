#pragma once

// Host stand-in for ESP-IDF's esp_err.h, for `pio test -e native`.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
