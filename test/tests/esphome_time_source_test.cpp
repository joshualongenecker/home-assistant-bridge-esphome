/*!
 * @file
 * @brief Unit tests for the ESPHome time source adapter.
 *
 * Validates that the time source returns a valid interface and correctly
 * delegates to esphome::millis().
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_time_source.h"
#include "double/esphome_hal_double.hpp"

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(esphome_time_source)
{
  i_tiny_time_source_t* time_source;

  void setup()
  {
    mock().strictOrder();
    esphome_hal_double_set_millis(0);
    time_source = esphome_time_source_init();
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(esphome_time_source, init_returns_non_null_interface)
{
  CHECK(time_source != nullptr);
}

TEST(esphome_time_source, init_returns_non_null_api)
{
  CHECK(time_source->api != nullptr);
}

/* ------------------------------------------------------------------ */
/* ticks() tests                                                        */
/* ------------------------------------------------------------------ */

TEST(esphome_time_source, ticks_returns_zero_at_startup)
{
  CHECK_EQUAL((tiny_time_source_ticks_t)0, time_source->api->ticks(time_source));
}

TEST(esphome_time_source, ticks_returns_current_millis)
{
  esphome_hal_double_set_millis(12345);
  CHECK_EQUAL((tiny_time_source_ticks_t)12345, time_source->api->ticks(time_source));
}

TEST(esphome_time_source, ticks_returns_updated_value_when_millis_changes)
{
  esphome_hal_double_set_millis(1000);
  CHECK_EQUAL((tiny_time_source_ticks_t)1000, time_source->api->ticks(time_source));

  esphome_hal_double_set_millis(2000);
  CHECK_EQUAL((tiny_time_source_ticks_t)2000, time_source->api->ticks(time_source));
}

TEST(esphome_time_source, ticks_returns_max_uint32_at_boundary)
{
  esphome_hal_double_set_millis(0xFFFFFFFF);
  CHECK_EQUAL((tiny_time_source_ticks_t)0xFFFFFFFF, time_source->api->ticks(time_source));
}
