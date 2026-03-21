#include "update_checker.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <cstdio>
#include <memory>

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG = "geappliances_bridge.update";

// Maximum bytes to read from the GitHub API response.
// Using heap allocation (see below) so this can be large enough to always
// include the "body" field which appears near the end of the response.
static constexpr size_t RESPONSE_BUF_SIZE = 8192;

// Discovery config JSON buffer: sized to hold all fixed strings plus two
// device_id copies (each up to ~64 chars) and the installed version string.
static constexpr size_t DISCOVERY_JSON_BUF_SIZE = 768;

// Release URL buffer: base URL (~80 chars) plus tag ("v" + MAX_VERSION_BUF_SIZE).
static constexpr size_t RELEASE_URL_BUF_SIZE = 256;

// ---------------------------------------------------------------------------
// Helper: extract a JSON string value from a JSON document.
//
// 'src' must point to the first character AFTER the opening '"' of the
// value.  The function copies the raw (still-JSON-escaped) content into
// 'out_buf', stopping at the first unescaped '"' or when 'max_len-1' bytes
// have been written.  The output is always NUL-terminated.
//
// Escape-safety on truncation: a lone backslash is never written without its
// companion character — the loop breaks before copying an incomplete 2-byte
// escape sequence.  This means truncated output is always valid JSON string
// content (no dangling backslash).
//
// Returns a pointer to the character just past the closing '"' on success,
// or nullptr if the closing '"' was not found before the buffer was full or
// the end of the input was reached (i.e. the output was truncated).
// ---------------------------------------------------------------------------
static const char *extract_json_string(const char *src, char *out_buf, size_t max_len) {
  size_t pos = 0;
  const char *p = src;

  while (*p != '\0' && pos < max_len - 1) {
    if (*p == '\\') {
      // Escape sequence: copy both the backslash and the following char
      // so the JSON-escaped content is preserved verbatim.
      if (pos + 1 >= max_len - 1) {
        break;  // no room for a 2-byte escape; stop here (escape-safe)
      }
      out_buf[pos++] = *p++;
      if (*p != '\0') {
        out_buf[pos++] = *p++;
      }
    } else if (*p == '"') {
      // Unescaped closing quote – end of the JSON string value.
      out_buf[pos] = '\0';
      return p + 1;
    } else {
      out_buf[pos++] = *p++;
    }
  }

  out_buf[pos] = '\0';
  return nullptr;  // truncated or malformed
}

bool fetch_latest_release_from_github(std::string &out_version,
                                      std::string &out_notes) {
  // Use a heap-allocated buffer so RESPONSE_BUF_SIZE can be large (8 kB)
  // without blowing the FreeRTOS task stack.
  auto buf = std::unique_ptr<char[]>(new (std::nothrow) char[RESPONSE_BUF_SIZE]());
  if (!buf) {
    ESP_LOGW(TAG, "Failed to allocate response buffer");
    return false;
  }

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
    return false;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGW(TAG, "GitHub API returned HTTP %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  // Read up to RESPONSE_BUF_SIZE-1 bytes; the buffer was zero-initialised so
  // it is always NUL-terminated.
  bytes_read = esp_http_client_read(client, buf.get(), static_cast<int>(RESPONSE_BUF_SIZE) - 1);
  if (bytes_read > 0) {
    buf[bytes_read] = '\0';
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (bytes_read <= 0) {
    ESP_LOGW(TAG, "Empty response from GitHub API");
    return false;
  }

  // ---- Extract "tag_name" ----
  const char *tag_key = "\"tag_name\":\"";
  const char *pos = strstr(buf.get(), tag_key);
  if (pos == nullptr) {
    ESP_LOGW(TAG, "tag_name not found in GitHub API response");
    return false;
  }

  char version_buf[MAX_VERSION_BUF_SIZE] = {0};
  const char *tag_end = extract_json_string(pos + strlen(tag_key),
                                            version_buf, sizeof(version_buf));
  if (tag_end == nullptr) {
    // nullptr means the closing '"' was not found — the response was truncated
    // or malformed before the tag_name value ended.  Reject to avoid using a
    // partial version string.
    ESP_LOGW(TAG, "tag_name value truncated or malformed in GitHub API response");
    return false;
  }

  if (version_buf[0] == '\0') {
    ESP_LOGW(TAG, "tag_name is empty in GitHub API response");
    return false;
  }

  std::string tag(version_buf);
  // Strip a leading "v" so the version string is bare (e.g. "1.0.0").
  if (!tag.empty() && tag[0] == 'v') {
    tag = tag.substr(1);
  }
  out_version = tag;

  // ---- Extract "body" (release notes) ----
  // The body value is raw JSON-escaped markdown.  We store it as-is so it
  // can be embedded directly in the state JSON payload without re-escaping.
  // extract_json_string() guarantees escape-safety even when the content is
  // truncated (it never writes a lone backslash), so the stored content is
  // always valid JSON string content.
  const char *body_key = "\"body\":\"";
  const char *body_pos = strstr(buf.get(), body_key);
  if (body_pos != nullptr) {
    char notes_buf[MAX_RELEASE_NOTES_BUF_SIZE] = {0};
    const char *notes_end = extract_json_string(body_pos + strlen(body_key),
                                                notes_buf, sizeof(notes_buf));
    if (notes_end == nullptr) {
      ESP_LOGD(TAG, "Release notes truncated to %zu bytes (buffer limit)",
               MAX_RELEASE_NOTES_BUF_SIZE - 1);
    }
    out_notes = notes_buf;
  } else {
    out_notes = "";
  }

  return true;
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
                          const std::string &latest_version,
                          const std::string &release_notes) {
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

  // Build the state JSON using std::string so the release_notes (which may be
  // up to MAX_RELEASE_NOTES_BUF_SIZE bytes) doesn't require a massive fixed buffer.
  // version strings and the release_url contain only safe characters (no
  // quotes or backslashes), so simple concatenation is safe.
  const std::string &effective_latest =
      latest_version.empty() ? installed_version : latest_version;

  std::string state_json =
      std::string("{")
      + "\"installed_version\":\"" + installed_version + "\","
      + "\"latest_version\":\""    + effective_latest  + "\","
      + "\"title\":\"GE Appliances Bridge\","
      + "\"release_url\":\""       + release_url       + "\"";

  if (!release_notes.empty()) {
    // release_notes is the raw content of GitHub's JSON "body" string value —
    // already JSON-escaped (newlines as \n, quotes as \", etc.) and guaranteed
    // escape-safe even when truncated by extract_json_string().
    state_json += ",\"release_notes\":\"" + release_notes + "\"";
  }

  state_json += "}";

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
                          const std::string &latest_version,
                          const std::string &release_notes) {
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

  // Build the state JSON using std::string so the release_notes (which may be
  // up to MAX_RELEASE_NOTES_BUF_SIZE bytes) doesn't require a massive fixed buffer.
  // version strings and the release_url contain only safe characters (no
  // quotes or backslashes), so simple concatenation is safe.
  const std::string &effective_latest =
      latest_version.empty() ? installed_version : latest_version;

  std::string state_json =
      std::string("{")
      + "\"installed_version\":\"" + installed_version + "\","
      + "\"latest_version\":\""    + effective_latest  + "\","
      + "\"title\":\"GE Appliances Bridge\","
      + "\"release_url\":\""       + release_url       + "\"";

  if (!release_notes.empty()) {
    // release_notes is already JSON-escaped (raw content from the GitHub API
    // JSON string value), so it can be embedded verbatim as a JSON string.
    state_json += ",\"release_notes\":\"" + release_notes + "\"";
  }

  state_json += "}";

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
