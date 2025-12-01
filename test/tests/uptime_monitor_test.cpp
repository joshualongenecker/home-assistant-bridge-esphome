/*!
 * @file
 * @brief
 */

extern "C" {
#include <stdio.h>
#include "uptime_monitor.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/mqtt_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

TEST_GROUP(uptime_monitor)
{
  uptime_monitor_t self;

  tiny_timer_group_double_t timer_group;
  mqtt_client_double_t mqtt_client;

  void setup()
  {
    mock().strictOrder();

    tiny_timer_group_double_init(&timer_group);
    mqtt_client_double_init(&mqtt_client);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

#define should_publish_uptime(seconds)                                                                       \
  do {                                                                                                       \
    char expected_payload[16];                                                                               \
    snprintf(expected_payload, sizeof(expected_payload), "%u", seconds);                                     \
    mock()                                                                                                   \
      .expectOneCall("publish_topic")                                                                        \
      .onObject(&mqtt_client)                                                                                \
      .withParameter("topic", "uptime")                                                                      \
      .withMemoryBufferParameter("payload", (const uint8_t*)expected_payload, strlen(expected_payload) + 1); \
  } while(0)

  void when_the_monitor_is_initialized()
  {
    uptime_monitor_init(
      &self,
      &timer_group.timer_group,
      &mqtt_client.interface);
  }

  void nothing_should_happen()
  {
  }
};

TEST(uptime_monitor, it_should_publish_the_uptime_every_second)
{
  should_publish_uptime(0);
  when_the_monitor_is_initialized();

  nothing_should_happen();
  after(999);

  should_publish_uptime(1);
  after(1);

  should_publish_uptime(2);
  after(1000);

  should_publish_uptime(3);
  after(1000);
}
