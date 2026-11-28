#ifndef ESP_HTTP_CLIENT_STUB_H
#define ESP_HTTP_CLIENT_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct esp_http_client {
    int dummy;
} esp_http_client;
typedef esp_http_client* esp_http_client_handle_t;

typedef struct {
    const char* url;
    void* (*crt_bundle_attach)(void*);
    int timeout_ms;
    int max_redirection_count;
} esp_http_client_config_t;

#define ESP_OK 0

static inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*) {
    return (esp_http_client_handle_t)0x1;
}
static inline int esp_http_client_open(esp_http_client_handle_t, int) { return ESP_OK; }
static inline void esp_http_client_fetch_headers(esp_http_client_handle_t) { }
static inline int esp_http_client_get_status_code(esp_http_client_handle_t) { return 200; }
static inline int esp_http_client_read(esp_http_client_handle_t, void*, int len) {
    (void)len; return 0;
}
static inline void esp_http_client_cleanup(esp_http_client_handle_t) { }

#ifdef __cplusplus
}
#endif

#endif
