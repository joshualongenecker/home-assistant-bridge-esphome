#pragma once

#include <cstdint>

extern "C" {
#include "erd_cache.h"
#include "erd_cache_mqtt_publisher.h"
}

#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace geappliances_bridge {

class DiagnosticSensorPublisher {
 public:
  void init(
      sensor::Sensor* erd_publish_rate_sensor,
      sensor::Sensor* mqtt_publish_rate_sensor,
      sensor::Sensor* erd_cache_entries_sensor,
      sensor::Sensor* erd_cache_updates_sensor,
      sensor::Sensor* mqtt_disconnect_count_sensor,
      sensor::Sensor* mqtt_disconnect_duration_sensor,
      erd_cache_t& erd_cache,
      erd_cache_mqtt_publisher_t& erd_cache_publisher);

  void loop();

  void set_erd_publish_rate_sensor(sensor::Sensor* sensor) { erd_publish_rate_sensor_ = sensor; }
  void set_mqtt_publish_rate_sensor(sensor::Sensor* sensor) { mqtt_publish_rate_sensor_ = sensor; }
  void set_erd_cache_entries_sensor(sensor::Sensor* sensor) { erd_cache_entries_sensor_ = sensor; }
  void set_erd_cache_updates_sensor(sensor::Sensor* sensor) { erd_cache_updates_sensor_ = sensor; }
  void set_mqtt_disconnect_count_sensor(sensor::Sensor* sensor) { mqtt_disconnect_count_sensor_ = sensor; }
  void set_mqtt_disconnect_duration_sensor(sensor::Sensor* sensor) { mqtt_disconnect_duration_sensor_ = sensor; }

 private:
  static constexpr uint32_t ERD_PUBLISH_RATE_INTERVAL_MS = 60000;

  uint32_t last_erd_publish_rate_publish_{0};
  uint32_t last_erd_cache_stats_publish_{0};
  uint32_t last_mqtt_disconnect_stats_publish_{0};

  sensor::Sensor* erd_publish_rate_sensor_{nullptr};
  sensor::Sensor* mqtt_publish_rate_sensor_{nullptr};
  sensor::Sensor* erd_cache_entries_sensor_{nullptr};
  sensor::Sensor* erd_cache_updates_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_count_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_duration_sensor_{nullptr};

  erd_cache_t* erd_cache_{nullptr};
  erd_cache_mqtt_publisher_t* erd_cache_publisher_{nullptr};
};

}
}