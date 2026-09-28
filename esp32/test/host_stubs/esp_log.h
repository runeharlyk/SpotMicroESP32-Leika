#pragma once

// Host stand-in for ESP-IDF's esp_log.h, for `pio test -e native`: logs go to stderr.
#include <cstdio>

#define ESP_LOG_HOST(level, tag, format, ...) std::fprintf(stderr, level " (%s) " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) ESP_LOG_HOST("E", tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) ESP_LOG_HOST("W", tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) ESP_LOG_HOST("I", tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ((void)0)
