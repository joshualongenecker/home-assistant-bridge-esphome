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

/* Depth of nested host critical sections. It is shared static state and the
 * concurrency suites run real threads that log and enter critical sections, so
 * it is only reachable through these mutex-guarded accessors (ThreadSanitizer
 * flagged the raw global). */
int esp_test_critical_depth_get_(void);
void esp_test_critical_depth_add_(int delta);
void esp_test_critical_depth_reset_(void);

/* Log-call recording. The ESP_LOGx stubs in double/esphome_hal_double.hpp call
 * esp_test_log_record_() with the tag and the critical-section depth at the call
 * site, so a test can assert a warning was emitted at depth 0: the dishwasher
 * crash of issue #198 was a log taken inside a critical section, which is not
 * observable from the publish-side depth alone. Tags are matched by string, so
 * a TAG must be a static string literal (every component file's `TAG` is); a
 * runtime-formatted TAG would dangle. Include discipline: when a TU also
 * includes test/include/esp-idf/esp_log.h, whichever header defines ESP_LOGx
 * last wins for that TU -- keep esphome_hal_double.hpp last so logs record. */
void esp_test_log_record_(const char* tag, int depth);
void esp_test_log_reset_(void);
int esp_test_log_count_(const char* tag);
int esp_test_log_count_at_depth_(const char* tag, int depth);
int esp_test_log_depth_(const char* tag);

#ifdef __cplusplus
}
#endif

#endif
