#include "diagnostic_sensor_publisher.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace geappliances_bridge {

void DiagnosticSensorPublisher::init(
    sensor::Sensor* erd_publish_rate_sensor,
    sensor::Sensor* mqtt_publish_rate_sensor,
    sensor::Sensor* erd_cache_entries_sensor,
    sensor::Sensor* erd_cache_updates_sensor,
    sensor::Sensor* mqtt_disconnect_count_sensor,
    sensor::Sensor* mqtt_disconnect_duration_sensor,
    erd_cache_t& erd_cache,
    erd_cache_mqtt_publisher_t& erd_cache_publisher)
{
  this->erd_publish_rate_sensor_ = erd_publish_rate_sensor;
  this->mqtt_publish_rate_sensor_ = mqtt_publish_rate_sensor;
  this->erd_cache_entries_sensor_ = erd_cache_entries_sensor;
  this->erd_cache_updates_sensor_ = erd_cache_updates_sensor;
  this->mqtt_disconnect_count_sensor_ = mqtt_disconnect_count_sensor;
  this->mqtt_disconnect_duration_sensor_ = mqtt_disconnect_duration_sensor;
  this->erd_cache_ = &erd_cache;
  this->erd_cache_publisher_ = &erd_cache_publisher;
}

template<typename GetA, typename GetB>
static bool publish_sensor_group(sensor::Sensor* a, sensor::Sensor* b,
                                 uint32_t* last_publish, uint32_t interval_ms,
                                 GetA get_a, GetB get_b)
{
  if ((a == nullptr) && (b == nullptr)) {
    return false;
  }
  uint32_t now = esphome::millis();
  if (now - *last_publish < interval_ms) {
    return false;
  }
  if (a != nullptr) {
    a->publish_state(static_cast<float>(get_a()));
  }
  if (b != nullptr) {
    b->publish_state(static_cast<float>(get_b()));
  }
  *last_publish = now;
  return true;
}

void DiagnosticSensorPublisher::loop() {
  publish_sensor_group(
      this->erd_publish_rate_sensor_,
      this->mqtt_publish_rate_sensor_,
      &this->last_erd_publish_rate_publish_,
      ERD_PUBLISH_RATE_INTERVAL_MS,
      [this]() -> uint32_t { return erd_cache_get_update_rate(this->erd_cache_); },
      [this]() -> uint32_t { return erd_cache_mqtt_publisher_get_publish_rate(this->erd_cache_publisher_); });

  publish_sensor_group(
      this->erd_cache_entries_sensor_,
      this->erd_cache_updates_sensor_,
      &this->last_erd_cache_stats_publish_,
      ERD_PUBLISH_RATE_INTERVAL_MS,
      [this]() -> uint32_t { return erd_cache_get_count(this->erd_cache_); },
      [this]() -> uint32_t { return erd_cache_get_required_update_rate(this->erd_cache_); });

  publish_sensor_group(
      this->mqtt_disconnect_count_sensor_,
      this->mqtt_disconnect_duration_sensor_,
      &this->last_mqtt_disconnect_stats_publish_,
      ERD_PUBLISH_RATE_INTERVAL_MS,
      [this]() -> uint32_t { return erd_cache_mqtt_publisher_get_disconnect_count(this->erd_cache_publisher_); },
      [this]() -> uint32_t { return erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(this->erd_cache_publisher_); });
}

}
}