/*!
 * @file
 * @brief Stub for esphome/components/mqtt/mqtt_client.h — provides MQTTClientComponent for tests.
 */

#ifndef esphome_components_mqtt_mqtt_client_h
#define esphome_components_mqtt_mqtt_client_h

#include <string>
#include <functional>
#include <cstdint>

namespace esphome {
namespace mqtt {


enum class MQTTClientDisconnectReason : int8_t {
  TCP_DISCONNECTED = 0,
  MQTT_UNACCEPTABLE_PROTOCOL_VERSION = 1,
  MQTT_IDENTIFIER_REJECTED = 2,
  MQTT_SERVER_UNAVAILABLE = 3,
  MQTT_MALFORMED_CREDENTIALS = 4,
  MQTT_NOT_AUTHORIZED = 5,
};
class MQTTClientComponent {
 public:
  virtual ~MQTTClientComponent() {}
  virtual bool is_connected() = 0;
  virtual bool publish(const std::string& topic, const std::string& payload,
                       uint8_t qos, bool retain) = 0;
  virtual bool publish(const char* topic, const char* payload, size_t payload_length,
                       uint8_t qos, bool retain) = 0;
  virtual void subscribe(const std::string& topic,
                         std::function<void(const std::string&, const std::string&)> callback,
                         uint8_t qos) = 0;
  virtual void unsubscribe(const std::string& topic) = 0;

  using on_connect_callback_t = void(bool session_present);
  using on_disconnect_callback_t = void(MQTTClientDisconnectReason reason);
  virtual void set_on_connect(std::function<on_connect_callback_t> &&callback) = 0;
  virtual void set_on_disconnect(std::function<on_disconnect_callback_t> &&callback) = 0;

  virtual void set_enable_on_boot(bool /*enable_on_boot*/) {}
  virtual void enable() {}
  virtual void disable() {}

  static MQTTClientComponent* global_mqtt_client;
};

/* Namespace-level global for code that accesses esphome::mqtt::global_mqtt_client directly */
inline MQTTClientComponent* global_mqtt_client = nullptr;

}  // namespace mqtt
}  // namespace esphome

#endif  // esphome_components_mqtt_mqtt_client_h
