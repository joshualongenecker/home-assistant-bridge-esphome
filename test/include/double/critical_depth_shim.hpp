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

#ifdef __cplusplus
}
#endif

#endif /* critical_depth_shim_hpp */
