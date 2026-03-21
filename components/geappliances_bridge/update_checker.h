#pragma once

#include <string>

namespace esphome {
namespace geappliances_bridge {

// Current version of the GE Appliances Bridge component.
// Bump this on every GitHub release and tag the commit with the same version
// (e.g. tag "v1.0.0" → GEAPPLIANCES_BRIDGE_VERSION "1.0.0").
static constexpr const char *GEAPPLIANCES_BRIDGE_VERSION = "1.0.0";

// GitHub Releases API URL used to check for newer versions.
static constexpr const char *GITHUB_RELEASES_API_URL =
    "https://api.github.com/repos/joshualongenecker/"
    "home-assistant-bridge-esphome/releases/latest";

// GitHub Releases page URL (used in the release_url state field).
static constexpr const char *GITHUB_RELEASES_URL =
    "https://github.com/joshualongenecker/"
    "home-assistant-bridge-esphome/releases";

// Maximum length of a version string stored in the shared buffer
// (e.g. "10.255.255" = 10 chars; 32 gives ample headroom).
static constexpr size_t MAX_VERSION_BUF_SIZE = 32;

// Fetch the tag_name of the latest GitHub release (strips leading "v").
// Performs a blocking HTTPS GET; call only from a background task.
// Returns an empty string on any error.
std::string fetch_latest_version_from_github();

// Publish the MQTT discovery config that registers an Update entity in
// Home Assistant.  Call once (or after reconnect) when MQTT is connected.
void publish_update_discovery(const std::string &device_id,
                              const std::string &installed_version,
                              const std::string &latest_version);

// Publish the MQTT state payload that Home Assistant reads to determine
// whether an update is available.  latest_version may equal installed_version
// when the firmware is already up to date.
void publish_update_state(const std::string &device_id,
                          const std::string &installed_version,
                          const std::string &latest_version);

}  // namespace geappliances_bridge
}  // namespace esphome
