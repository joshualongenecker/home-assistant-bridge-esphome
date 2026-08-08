/*!
 * @file
 * @brief Concrete test implementation of MQTTClientComponent that stores
 *        subscription callbacks so they can be invoked manually in tests.
 *
 * IMPORTANT: This header must be included AFTER undefing CppUTest's new
 * macro, since it depends on <functional> which uses placement new.
 */

#ifndef mqtt_test_double_hpp
#define mqtt_test_double_hpp

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome/components/mqtt/mqtt_client.h"
#include <string>
#include <functional>

namespace esphome {
namespace mqtt {

class MqttTestDouble : public MQTTClientComponent {
 public:
  bool connected_{false};
  bool publish_should_fail_{false};
  std::string last_published_topic_;
  std::string last_published_payload_;

  std::function<void(const std::string&, const std::string&)> subscribe_callback_;
  std::function<void(bool)> on_connect_callback_;
  std::function<void(MQTTClientDisconnectReason)> on_disconnect_callback_;

  bool is_connected() override { return connected_; }
  bool publish(const std::string& topic, const std::string& payload,
               uint8_t /*qos*/, bool /*retain*/) override {
    last_published_topic_ = topic;
    last_published_payload_ = payload;
    return !publish_should_fail_;
  }

  bool publish(const char* topic, const char* payload, size_t payload_length,
               uint8_t /*qos*/, bool /*retain*/) override {
    last_published_topic_ = topic;
    last_published_payload_.assign(payload, payload_length);
    return !publish_should_fail_;
  }

  void subscribe(const std::string& /*topic*/,
                 std::function<void(const std::string&, const std::string&)> callback,
                 uint8_t /*qos*/) override {
    subscribe_callback_ = callback;
  }

  void unsubscribe(const std::string& /*topic*/) override { }

  void set_on_connect(std::function<on_connect_callback_t>&& callback) override {
    on_connect_callback_ = std::move(callback);
  }

  void set_on_disconnect(std::function<on_disconnect_callback_t>&& callback) override {
    on_disconnect_callback_ = std::move(callback);
  }

  void simulate_message(const std::string& topic, const std::string& payload) {
    if (subscribe_callback_) {
      subscribe_callback_(topic, payload);
    }
  }
};

}  // namespace mqtt
}  // namespace esphome

#endif
