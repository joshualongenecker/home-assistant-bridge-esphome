#include "geappliances_bridge_health_sensor.h"
#include "../geappliances_bridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG __attribute__((unused)) = "geappliances_bridge.health";

void GeappliancesBridgeHealthSensor::update() {
  if (this->parent_ == nullptr) {
    return;
  }

  float value = NAN;

  switch (this->sensor_type_) {
    case SENSOR_TYPE_PENDING_UPDATES: {
      size_t pending = this->parent_->get_pending_mqtt_updates();
      value = static_cast<float>(pending);
      break;
    }
    case SENSOR_TYPE_POLLING_CYCLE_TIME_MS: {
      uint32_t cycle_ms = this->parent_->get_polling_cycle_time_ms();
      value = static_cast<float>(cycle_ms);
      break;
    }
    case SENSOR_TYPE_POLLING_CYCLE_COUNT: {
      uint32_t count = this->parent_->get_polling_cycle_count();
      value = static_cast<float>(count);
      break;
    }
  }

  this->publish_state(value);
}

void GeappliancesBridgeHealthSensor::dump_config() {
  switch (this->sensor_type_) {
    case SENSOR_TYPE_PENDING_UPDATES:
      ESP_LOGCONFIG(TAG, "  Pending MQTT Updates Sensor");
      break;
    case SENSOR_TYPE_POLLING_CYCLE_TIME_MS:
      ESP_LOGCONFIG(TAG, "  Polling Cycle Time Sensor");
      break;
    case SENSOR_TYPE_POLLING_CYCLE_COUNT:
      ESP_LOGCONFIG(TAG, "  Polling Cycle Count Sensor");
      break;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome