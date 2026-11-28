#ifndef ESP_LOG_STUB_H
#define ESP_LOG_STUB_H

#include "esphome/core/log.h"

#ifndef ESP_LOGE
#define ESP_LOGE(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) ESP_LOGV(tag, __VA_ARGS__)
#endif

#endif
