#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdio.h>

#define ESP_LOGD(tag, fmt, ...) ((void)printf("[D] %s: " fmt "\n", tag, ##__VA_ARGS__))
#define ESP_LOGI(tag, fmt, ...) ((void)printf("[I] %s: " fmt "\n", tag, ##__VA_ARGS__))
#define ESP_LOGW(tag, fmt, ...) ((void)printf("[W] %s: " fmt "\n", tag, ##__VA_ARGS__))
#define ESP_LOGE(tag, fmt, ...) ((void)printf("[E] %s: " fmt "\n", tag, ##__VA_ARGS__))

#endif
