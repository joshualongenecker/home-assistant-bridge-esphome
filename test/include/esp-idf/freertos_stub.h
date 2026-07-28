/*!
 * @file
 * @brief ESP-IDF FreeRTOS stubs for test/simulation builds.
 *
 * Provides minimal definitions of FreeRTOS types and functions so that
 * ESP-IDF-gated code compiles and exercises its logic in test builds.
 */

#ifndef ESP_FREERTOS_STUB_H
#define ESP_FREERTOS_STUB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- FreeRTOS.h types ---------- */
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

/* ---------- task.h types & functions ---------- */
typedef void* TaskHandle_t;
typedef void* StackType_t;

struct TaskControlBlock_t {
    void* dummy;
};
typedef struct TaskControlBlock_t StaticTask_t;

#define pdMS_TO_TICKS(ms) ((ms))
#define portMAX_DELAY ((UBaseType_t)-1)
#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)

static inline void vTaskDelay(UBaseType_t ticks) { (void)ticks; }
static inline void vTaskDelete(TaskHandle_t) { }
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1024; }
static inline TaskHandle_t xTaskCreateStatic(void (*fn)(void*), const char* name,
        uint32_t stack, void* arg, UBaseType_t prio,
        StackType_t* stack_buf, StaticTask_t* tcb) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    (void)stack_buf; (void)tcb;
    return (TaskHandle_t)0x1;
}

/* ---------- queue.h types & functions ---------- */
typedef void* QueueHandle_t;

static inline QueueHandle_t xQueueCreateStatic(uint32_t count, uint32_t size,
                                                uint8_t* storage, StaticTask_t* tcb) {
    (void)count; (void)size; (void)storage; (void)tcb;
    return (QueueHandle_t)0x1;
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void* data, UBaseType_t ticks) {
    (void)q; (void)data; (void)ticks;
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void* data, UBaseType_t ticks) {
    (void)q; (void)data; (void)ticks;
    return pdFALSE;
}
static inline void vQueueDelete(QueueHandle_t) { }

/* ---------- semphr.h types & functions ---------- */
typedef void* SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (SemaphoreHandle_t)0x1;
}
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)0x2;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, UBaseType_t ticks) {
    (void)sem; (void)ticks;
    return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    (void)sem;
    return pdTRUE;
}
static inline void vSemaphoreDelete(SemaphoreHandle_t) { }

/* ---------- Critical section ---------- */
typedef void* portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED (NULL)
#define taskENTER_CRITICAL(x) do { (void)(x); } while (0)
#define taskEXIT_CRITICAL(x) do { (void)(x); } while (0)

/* ---------- Task state ---------- */
typedef enum {
    eReady,
    eRunning,
    eBlocked,
    eSuspended,
    eDeleted
} eTaskState;

static inline eTaskState eTaskGetState(TaskHandle_t) { return eDeleted; }

#ifdef __cplusplus
}
#endif

#endif /* ESP_FREERTOS_STUB_H */
