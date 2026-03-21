#pragma once

#include <string>

namespace esphome {
namespace geappliances_bridge {

// Current version of the GE Appliances Bridge component.
// Bump this on every GitHub release and tag the commit with the same version
// (e.g. tag "v1.0.0" → GEAPPLIANCES_BRIDGE_VERSION "1.0.0").
// Must be a preprocessor macro so it can be concatenated with string literals
// (e.g. for the HTTP User-Agent header).
#define GEAPPLIANCES_BRIDGE_VERSION "1.0.0"

// GitHub Releases API URL used to check for newer versions.
static constexpr const char *GITHUB_RELEASES_API_URL =
    "https://api.github.com/repos/joshualongenecker/"
    "home-assistant-bridge-esphome/releases/latest";

// Maximum length of a version string (e.g. "10.255.255" = 10 chars; 32 gives headroom).
static constexpr size_t MAX_VERSION_BUF_SIZE = 32;

// Fetch the latest GitHub release tag (strips leading "v") into out_version.
// Performs a blocking HTTPS GET; call only from a background task.
// Returns true on success, false on any error.
bool fetch_latest_release_from_github(std::string &out_version);

}  // namespace geappliances_bridge
}  // namespace esphome
