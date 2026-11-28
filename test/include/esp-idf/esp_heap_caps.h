#ifndef ESP_HEAP_CAPS_STUB_H
#define ESP_HEAP_CAPS_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_DMA 0x04
#define MALLOC_CAP_8BIT 0x08

static inline void* heap_caps_realloc(void* rmem, size_t newsize, uint32_t caps) {
    (void)caps; return realloc(rmem, newsize);
}
static inline void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps; return malloc(size);
}
static inline void heap_caps_free(void* mem) { free(mem); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 81920; }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 40960; }

#ifdef __cplusplus
}
#endif

#endif
