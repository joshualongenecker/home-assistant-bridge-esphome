#ifndef ESP_TASK_WDT_STUB_H
#define ESP_TASK_WDT_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

static inline void esp_task_wdt_reset(void) { }
static inline int esp_task_wdt_add(void*) { return 0; }
static inline int esp_task_wdt_delete(void*) { return 0; }

#ifdef __cplusplus
}
#endif

#endif
