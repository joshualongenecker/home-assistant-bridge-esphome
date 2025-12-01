/*!
 * @file
 * @brief
 *
 * Writes the approximate system uptime to an MQTT topic every second.
 */

#ifndef uptime_monitor_h
#define uptime_monitor_h

#include "i_mqtt_client.h"
#include "tiny_timer.h"

typedef struct {
  tiny_timer_group_t* timer_group;
  i_mqtt_client_t* mqtt_client;
  tiny_timer_t long_timer;
  tiny_timer_t second_timer;
  tiny_timer_ticks_t last_remaining_ticks;
  uint64_t elapsed_msec;
} uptime_monitor_t;

/*!
 * Initialize an uptime monitor.
 */
void uptime_monitor_init(uptime_monitor_t* self, tiny_timer_group_t* timer_group, i_mqtt_client_t* mqtt_client);

#endif
