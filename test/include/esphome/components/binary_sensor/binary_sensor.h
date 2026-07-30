/*! 
 * @file
 * @brief Minimal BinarySensor stub for bridge unit tests.
 */

#ifndef esphome_components_binary_sensor_binary_sensor_h
#define esphome_components_binary_sensor_binary_sensor_h

#include <functional>
#include <utility>

namespace esphome {
namespace binary_sensor {

class BinarySensor {
 public:
  bool state{false};

  bool has_state() const { return has_state_; }

  template<typename F> void add_on_state_callback(F &&callback) {
    callback_ = std::forward<F>(callback);
  }

  void publish_state(bool new_state) {
    state = new_state;
    has_state_ = true;
    if (callback_) {
      callback_(state);
    }
  }

 private:
  bool has_state_{false};
  std::function<void(bool)> callback_;
};

}  // namespace binary_sensor
}  // namespace esphome

#endif  // esphome_components_binary_sensor_binary_sensor_h
