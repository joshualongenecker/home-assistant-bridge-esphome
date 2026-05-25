/*!
 * @file
 * @brief HaDiscoveryManager – Home Assistant MQTT autodiscovery manager.
 *
 * Extracted from GeappliancesBridge as part of the god class refactoring.
 * Encapsulates the logic for:
 *   - Watching for "ready" signal (quiet window or polling list complete)
 *   - Spawning a FreeRTOS background task to fetch JSONL definitions via HTTPS
 *   - Parsing JSONL lines into MQTT discovery payloads
 *   - Rate-limited publishing of discovery messages to Home Assistant
 *
 * On non-ESP-IDF builds the fetch is a no-op and a warning is logged.
 */

#pragma once

#include <cstdint>
#include <set>
#include <string>

extern "C" {
#include "tiny_gea3_erd_client.h"
}

#ifdef USE_ESP_IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#endif

namespace esphome {
namespace mqtt {
class MQTTClientComponent;
}

namespace geappliances_bridge {

static constexpr uint32_t HA_DISCOVERY_QUIET_MS = 10000;
static constexpr uint32_t HA_DISCOVERY_MAX_WAIT_MS = 30000;  // 30s safety cap
static constexpr uint32_t HA_ENTITY_PUBLISH_INTERVAL_MS = 50;

enum HaDiscoveryState {
  HA_DISCOVERY_IDLE,
  HA_DISCOVERY_WAITING_FOR_READY,
  HA_DISCOVERY_DOWNLOADING,
  HA_DISCOVERY_PUBLISHING,
  HA_DISCOVERY_COMPLETE,
  HA_DISCOVERY_FAILED
};

/*!
 * A (topic, payload) pair ready to be published via MQTT.
 */
struct HaDiscoveryItem {
  std::string topic;
  std::string payload;
};

class HaDiscoveryManager {
 public:
  void init(const std::string& base_url,
            const std::string& device_id,
            const std::string& model_number,
            const std::string& serial_number,
            const std::set<tiny_erd_t>& registered_erds,
            bool generate_device_config);

  void set_registered_erds(const std::set<tiny_erd_t>& erds);

  void on_erd_seen(tiny_erd_t erd);

  void run(bool is_poll_mode,
           bool polling_list_complete,
           bool subscription_activity_detected,
           mqtt::MQTTClientComponent* mqtt_client);

  bool is_complete() const { return state_ == HA_DISCOVERY_COMPLETE; }
  bool is_failed()   const { return state_ == HA_DISCOVERY_FAILED; }

  HaDiscoveryState get_state() const { return state_; }

  /// Clean up resources (FreeRTOS task, queue, stack). Call from teardown.
  void cleanup();

 private:
  void publish_ha_discovery_(mqtt::MQTTClientComponent* mqtt_client);
  void publish_next_entity_(mqtt::MQTTClientComponent* mqtt_client);

#ifdef USE_ESP_IDF
  static void ha_fetch_task_fn_(void* param);
  void fetch_ha_definitions_();
  bool fetch_category_(const std::string& url,
                       const std::string& device_id,
                       const std::string& device_json);
  bool process_jsonl_line_(const std::string& line,
                           const std::string& device_id,
                           const std::string& device_json);
#endif

  std::string escape_json_str_(const std::string& s);
  std::string build_device_json_();

  HaDiscoveryState state_{HA_DISCOVERY_IDLE};
  std::string base_url_;
  std::string device_id_;
  std::string model_number_;
  std::string serial_number_;
  std::set<tiny_erd_t> registered_erds_;
  std::set<tiny_erd_t> registered_erds_snapshot_;
  std::set<tiny_erd_t> seen_erds_;
  bool generate_device_config_{false};
  uint32_t last_activity_{0};
  uint32_t last_publish_ms_{0};
  uint32_t start_time_{0};  // millis() when WAITING_FOR_READY state entered

#ifdef USE_ESP_IDF
  QueueHandle_t queue_{nullptr};
  TaskHandle_t  task_handle_{nullptr};
  StackType_t*  task_stack_{nullptr};
  StaticTask_t* task_tcb_{nullptr};
#endif
};

}  // namespace geappliances_bridge
}  // namespace esphome
