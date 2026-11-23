/*!
 * @file
 * @brief Host-side depth counter behind taskENTER_CRITICAL()/taskEXIT_CRITICAL().
 *
 * On the single-core parts (C3/C6) taskENTER_CRITICAL() raises the interrupt
 * threshold, so anything that blocks or touches stdio inside a critical section
 * is a fault waiting to happen: ESP-IDF's newlib lock layer calls abort() for a
 * recursive-mutex take in that context ("recursive mutexes make no sense in ISR
 * context"), and a publish that blocks on the outbound ring trips the interrupt
 * watchdog. See issue #198.
 *
 * The stub macros in esp-idf/freertos_stub.h maintain this counter, so a test
 * can assert (a) the code under test is at depth 0 when it publishes or logs,
 * and (b) it did not return out of a critical section.
 */

#ifndef critical_depth_shim_hpp
#define critical_depth_shim_hpp

#ifdef __cplusplus
extern "C" {
#endif

extern int esp_test_critical_depth_;

/* Log-call recording. The ESP_LOGx stubs in double/esphome_hal_double.hpp call
 * esp_test_log_record_() with the tag and the critical-section depth at the call
 * site, so a test can assert a warning was emitted at depth 0: the dishwasher
 * crash of issue #198 was a log taken inside a critical section, which is not
 * observable from the publish-side depth alone. Tags are matched by string. */
void esp_test_log_record_(const char* tag, int depth);
void esp_test_log_reset_(void);
int esp_test_log_count_(const char* tag);   /* calls recorded for tag; 0 if none */
int esp_test_log_depth_(const char* tag);   /* depth of the last call for tag; -1 if none */

#ifdef __cplusplus
}
#endif

#endif /* critical_depth_shim_hpp */
