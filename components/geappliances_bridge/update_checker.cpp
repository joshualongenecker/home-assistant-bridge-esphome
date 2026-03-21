#include "update_checker.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <cstdio>

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG = "geappliances_bridge.update";

// Maximum bytes to read from the GitHub API response.
// The releases/latest response is large; we only need the first portion
// which always contains the tag_name field.
static constexpr size_t RESPONSE_BUF_SIZE = 2048;

// Discovery config JSON buffer: sized to hold all fixed strings plus two
// device_id copies (each up to ~64 chars) and the installed version string.
static constexpr size_t DISCOVERY_JSON_BUF_SIZE = 768;

// State JSON buffer: holds installed_version, latest_version, title, and
// release_url (max ~100 chars each).
static constexpr size_t STATE_JSON_BUF_SIZE = 512;

// Release URL buffer: base URL (~80 chars) plus tag ("v" + MAX_VERSION_BUF_SIZE).
static constexpr size_t RELEASE_URL_BUF_SIZE = 256;

std::string fetch_latest_version_from_github() {
  char buf[RESPONSE_BUF_SIZE] = {0};
  int bytes_read = 0;

  esp_http_client_config_t cfg = {};
  cfg.url = GITHUB_RELEASES_API_URL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
  // GitHub API requires a non-empty User-Agent header.
  cfg.user_agent = "ESPHome-GEAppliances-Bridge/" GEAPPLIANCES_BRIDGE_VERSION;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    ESP_LOGW(TAG, "Failed to create HTTP client");
    return "";
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return "";
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGW(TAG, "GitHub API returned HTTP %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return "";
  }

  // Read up to RESPONSE_BUF_SIZE-1 bytes so the buffer is always NUL-terminated.
  bytes_read = esp_http_client_read(client, buf, static_cast<int>(RESPONSE_BUF_SIZE) - 1);
  if (bytes_read > 0) {
    buf[bytes_read] = '\0';
  } else {
    buf[0] = '\0';
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (bytes_read <= 0) {
    ESP_LOGW(TAG, "Empty response from GitHub API");
    return "";
  }

  // Extract "tag_name":"<value>" from the JSON response.
  // GitHub always emits this near the start of the payload so it fits in the
  // first RESPONSE_BUF_SIZE bytes.
  const char *key = "\"tag_name\":\"";
  const char *pos = strstr(buf, key);
  if (pos == nullptr) {
    ESP_LOGW(TAG, "tag_name not found in GitHub API response");
    return "";
  }

  pos += strlen(key);
  const char *end_quote = strchr(pos, '"');
  if (end_quote == nullptr || end_quote <= pos) {
    ESP_LOGW(TAG, "Could not parse tag_name value");
    return "";
  }

  std::string tag(pos, static_cast<size_t>(end_quote - pos));

  // Strip a leading "v" so the version strings are bare (e.g. "1.0.0").
  if (!tag.empty() && tag[0] == 'v') {
    tag = tag.substr(1);
  }

  return tag;
}

void publish_update_discovery(const std::string &device_id,
                              const std::string &installed_version,
                              const std::string &latest_version) {
  auto *mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) {
    return;
  }

  // Discovery topic follows the <prefix>/update/<object_id>/config pattern.
  std::string discovery_topic =
      "homeassistant/update/geappliances_bridge_" + device_id + "/config";

  // State / command topics are placed outside the homeassistant discovery
  // namespace so retained ERD topics do not interfere.
  std::string state_topic =
      "geappliances/" + device_id + "/bridge/update/state";
  std::string install_topic =
      "geappliances/" + device_id + "/bridge/update/install";

  // Build the discovery payload.
  char cfg_json[DISCOVERY_JSON_BUF_SIZE];
  snprintf(cfg_json, sizeof(cfg_json),
           "{"
           "\"name\":\"GE Appliances Bridge\","
           "\"unique_id\":\"geappliances_bridge_update_%s\","
           "\"state_topic\":\"%s\","
           "\"command_topic\":\"%s\","
           "\"payload_install\":\"install\","
           "\"device_class\":\"firmware\","
           "\"device\":{"
           "\"identifiers\":[\"geappliances_bridge_%s\"],"
           "\"name\":\"GE Appliances Bridge\","
           "\"manufacturer\":\"joshualongenecker\","
           "\"model\":\"ESP32-C3\","
           "\"sw_version\":\"%s\""
           "}"
           "}",
           device_id.c_str(),
           state_topic.c_str(),
           install_topic.c_str(),
           device_id.c_str(),
           installed_version.c_str());

  mqtt_client->publish(discovery_topic, cfg_json, 1, true);
  ESP_LOGI(TAG, "Published update entity discovery config (installed=%s, latest=%s)",
           installed_version.c_str(), latest_version.c_str());
}

void publish_update_state(const std::string &device_id,
                          const std::string &installed_version,
                          const std::string &latest_version) {
  auto *mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) {
    return;
  }

  std::string state_topic =
      "geappliances/" + device_id + "/bridge/update/state";

  // release_url points to the specific release when a newer version is known,
  // otherwise it links to the releases index.
  char release_url[RELEASE_URL_BUF_SIZE];
  if (!latest_version.empty() && latest_version != installed_version) {
    snprintf(release_url, sizeof(release_url),
             "%s/tag/v%s",
             GITHUB_RELEASES_URL,
             latest_version.c_str());
  } else {
    snprintf(release_url, sizeof(release_url), "%s", GITHUB_RELEASES_URL);
  }

  char state_json[STATE_JSON_BUF_SIZE];
  snprintf(state_json, sizeof(state_json),
           "{"
           "\"installed_version\":\"%s\","
           "\"latest_version\":\"%s\","
           "\"title\":\"GE Appliances Bridge\","
           "\"release_url\":\"%s\""
           "}",
           installed_version.c_str(),
           latest_version.empty() ? installed_version.c_str() : latest_version.c_str(),
           release_url);

  mqtt_client->publish(state_topic, state_json, 1, true);

  if (!latest_version.empty() && latest_version != installed_version) {
    ESP_LOGI(TAG, "Update available: v%s -> v%s",
             installed_version.c_str(), latest_version.c_str());
  } else {
    ESP_LOGI(TAG, "Firmware is up to date (v%s)", installed_version.c_str());
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
