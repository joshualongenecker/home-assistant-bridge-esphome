#include "update_checker.h"
#include "esphome/core/log.h"

#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <memory>

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG = "geappliances_bridge.update";

// Maximum bytes to read from the GitHub API response.
// The /tags endpoint returns an array; "name" is the first key in the first
// object so it always appears within the first few hundred bytes regardless
// of how many tags the repository has.  2 kB gives plenty of headroom.
static constexpr size_t RESPONSE_BUF_SIZE = 2048;

// ---------------------------------------------------------------------------
// Helper: extract a JSON string value from a JSON document.
//
// 'src' must point to the first character AFTER the opening '"' of the value.
// The function copies the raw content into 'out_buf', stopping at the first
// unescaped '"' or when 'max_len-1' bytes have been written.
// The output is always NUL-terminated.
// Returns a pointer past the closing '"' on success, or nullptr if truncated.
// ---------------------------------------------------------------------------
static const char *extract_json_string(const char *src, char *out_buf, size_t max_len) {
  size_t pos = 0;
  const char *p = src;

  while (*p != '\0' && pos < max_len - 1) {
    if (*p == '\\') {
      if (pos + 1 >= max_len - 1) break;
      out_buf[pos++] = *p++;
      if (*p != '\0') out_buf[pos++] = *p++;
    } else if (*p == '"') {
      out_buf[pos] = '\0';
      return p + 1;
    } else {
      out_buf[pos++] = *p++;
    }
  }

  out_buf[pos] = '\0';
  return nullptr;
}

bool fetch_latest_release_from_github(std::string &out_version) {
  auto buf = std::unique_ptr<char[]>(new (std::nothrow) char[RESPONSE_BUF_SIZE]());
  if (!buf) {
    ESP_LOGW(TAG, "Failed to allocate response buffer");
    return false;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = GITHUB_TAGS_API_URL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
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

  int bytes_read = esp_http_client_read(client, buf.get(), static_cast<int>(RESPONSE_BUF_SIZE) - 1);
  if (bytes_read > 0) {
    buf[bytes_read] = '\0';
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (bytes_read <= 0) {
    ESP_LOGW(TAG, "Empty response from GitHub API");
    return false;
  }

  // The /tags API returns a JSON array sorted newest-first.
  // The first element's "name" field is the latest tag (e.g. "v1.0.0").
  const char *name_key = "\"name\":\"";
  const char *pos = strstr(buf.get(), name_key);
  if (pos == nullptr) {
    ESP_LOGW(TAG, "Unable to find tag name in GitHub API response (truncated or no tags)");
    return false;
  }

  char version_buf[MAX_VERSION_BUF_SIZE] = {0};
  const char *tag_end = extract_json_string(pos + strlen(name_key), version_buf, sizeof(version_buf));
  if (tag_end == nullptr) {
    ESP_LOGW(TAG, "Tag name truncated or malformed in GitHub API response");
    return false;
  }
  if (version_buf[0] == '\0') {
    ESP_LOGW(TAG, "Tag name is empty in GitHub API response");
    return false;
  }

  std::string tag(version_buf);
  // Strip a leading "v" so the version string is bare (e.g. "1.0.0").
  if (!tag.empty() && tag[0] == 'v') {
    tag = tag.substr(1);
  }
  out_version = tag;
  return true;
}

}  // namespace geappliances_bridge
}  // namespace esphome

