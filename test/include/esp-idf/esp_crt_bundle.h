#ifndef ESP_CRT_BUNDLE_STUB_H
#define ESP_CRT_BUNDLE_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* esp_crt_bundle_handle_t;
static inline esp_crt_bundle_handle_t esp_crt_bundle_attach(void*) { return (esp_crt_bundle_handle_t)0x1; }

#ifdef __cplusplus
}
#endif

#endif
