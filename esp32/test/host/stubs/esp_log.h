#pragma once
// Host stand-in: firmware logging is silent in host tests.
#define ESP_LOGI(tag, ...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)0)
#define ESP_LOGE(tag, ...) ((void)0)
#define ESP_LOGV(tag, ...) ((void)0)
