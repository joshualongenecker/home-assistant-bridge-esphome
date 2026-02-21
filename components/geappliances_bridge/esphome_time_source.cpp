#include "esphome_time_source.h"
#include "esphome/core/hal.h"

static tiny_time_source_ticks_t esphome_time_source_ticks(i_tiny_time_source_t* self)
{
  (void)self;
  return esphome::millis();
}

static const i_tiny_time_source_api_t api = { esphome_time_source_ticks };

static i_tiny_time_source_t instance = { &api };

extern "C" i_tiny_time_source_t* esphome_time_source_init(void)
{
  return &instance;
}
