/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager implementation.
 *
 * Main-loop design: start() builds the sorted ERD list and device JSON
 * inline. run() is called from the main loop; it decompresses chunks,
 * parses JSONL, and publishes one entity per call, keeping loop times low.
 */

#include "ha_discovery_manager.h"
#include "ha_discovery_data.h"
#include "geappliances_bridge_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#ifndef USE_ESP_IDF_STUBS
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_STDIO
#include "miniz.h"
#endif

GEA_TAG(TAG) = "ha_discovery";

/* ------------------------------------------------------------------ */
/* Zero-allocation JSON parser helpers                                */
/* ------------------------------------------------------------------ */

/* Extract a JSON string value for the given key.
 * Returns a pointer to the first character of the value (after opening quote)
 * and sets *out_len to the length (not including closing quote).
 * Returns NULL if key not found or value is not a string. */
static const char* json_get_str(const char* json, const char* key,
                                 const char** out_value, size_t* out_len)
{
    size_t key_len = strlen(key);
    const char* p = json;

    while ((p = strstr(p, "\"")) != NULL) {
        /* Verify this " is the start of a key (preceded by {, ,, or [),
         * not a value that happens to match the key name. */
        if (p > json && *(p - 1) != '{' && *(p - 1) != ',' && *(p - 1) != '[') { p++; continue; }
        if (strncmp(p + 1, key, key_len) == 0 && p[key_len + 1] == '\"') {
            p = p + key_len + 3;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (*p == ':') p++;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (*p == '"') {
                *out_value = p + 1;
                const char* end = p + 1;
                while (*end && *end != '"') {
                    if (*end == '\\' && *(end + 1)) end++;  /* skip escaped char, guard against unterminated escape */
                    end++;
                }
                *out_len = (size_t)(end - *out_value);
                return *out_value;
            } else if (*p == '{' || *p == '[') {
                char open = *p;
                char close = (open == '{') ? '}' : ']';
                *out_value = p;
                int depth = 0;
                const char* end = p;
                while (*end) {
                    if (*end == open) depth++;
                    if (*end == close) {
                        depth--;
                        if (depth == 0) break;
                    }
                    end++;
                }
                *out_len = (size_t)(end - p + 1);
                return *out_value;
            } else {
                *out_value = p;
                const char* end = p;
                while (*end && *end != ',' && *end != '}' && *end != ']' && *end != '\n') {
                    end++;
                }
                *out_len = (size_t)(end - p);
                return *out_value;
            }
        }
        p++;
    }
    return NULL;
}

/* Unescape a JSON string value into out (max out_size bytes including null). */
static void json_unescape(const char* src, size_t src_len, char* out, int out_size)
{
    int i = 0;
    const char* p = src;
    const char* end = src + src_len;
    while (p < end && i < out_size - 1) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case '"':  out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/':  out[i++] = '/'; break;
                case 'n':  out[i++] = '\n'; break;
                case 'r':  out[i++] = '\r'; break;
                case 't':  out[i++] = '\t'; break;
                case 'u':  /* skip \uXXXX */ if (p + 4 < end) p += 4; break;
                default:   out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
}

/* Copy a raw JSON string value for embedding in another JSON string.
 * The input is already JSON-escaped (contains \\, \", etc.).
 * Embedding it directly in another JSON string requires no transformation —
 * the escape sequences remain valid.
 * Returns the number of bytes written (excluding null terminator). */
static int json_embed_value(const char* src, size_t src_len, char* out, int out_size)
{
    if (src_len >= (size_t)out_size) src_len = (size_t)(out_size - 1);
    memcpy(out, src, src_len);
    out[src_len] = '\0';
    return (int)src_len;
}

/* ------------------------------------------------------------------ */
/* ERD cache lookup (binary search on sorted array)                    */
/* ------------------------------------------------------------------ */

static bool erd_is_registered_sorted(const ha_discovery_manager_t* self, uint16_t erd_id)
{
    uint16_t lo = 0, hi = self->sorted_erds_count;
    while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        if (self->sorted_erds[mid] < erd_id) lo = mid + 1;
        else if (self->sorted_erds[mid] > erd_id) hi = mid;
        else return true;
    }
    return false;
}

/* Build sorted ERD array from cache for binary search. */
static void build_sorted_erd_list(ha_discovery_manager_t* self)
{
    self->sorted_erds_count = 0;
    uint16_t iterator = 0;
    while (true) {
        erd_cache_entry_t* entry = erd_cache_get_next_entry(self->cache, &iterator);
        if (!entry) break;
        if (self->sorted_erds_count >= HA_DISCOVERY_MAX_ERDS) break;
        /* Dedup */
        bool already = false;
        for (uint16_t k = 0; k < self->sorted_erds_count; k++) {
            if (self->sorted_erds[k] == entry->erd) { already = true; break; }
        }
        if (!already) {
            self->sorted_erds[self->sorted_erds_count++] = entry->erd;
        }
    }
    /* Insertion sort (small N). */
    for (uint16_t i = 1; i < self->sorted_erds_count; i++) {
        uint16_t key = self->sorted_erds[i];
        uint16_t j = i;
        while (j > 0 && self->sorted_erds[j - 1] > key) {
            self->sorted_erds[j] = self->sorted_erds[j - 1];
            j--;
        }
        self->sorted_erds[j] = key;
    }
}

/* ------------------------------------------------------------------ */
/* Device JSON builder                                                */
/* ------------------------------------------------------------------ */

static void build_device_json(ha_discovery_manager_t* self)
{
    if (self->device_id == NULL) {
        self->device_json_buf[0] = '\0';
        return;
    }
    int pos = snprintf(self->device_json_buf, sizeof(self->device_json_buf),
        "{\"identifiers\":[\"");

    /* Escape device_id for identifiers */
    for (const char* p = self->device_id; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
        else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
        else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
        else { self->device_json_buf[pos++] = (char)c; }
    }
    if (pos < (int)sizeof(self->device_json_buf) - 64) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos,
            "\"],\"name\":\"");
    }

    /* Escape device_id for name */
    for (const char* p = self->device_id; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
        else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
        else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
        else { self->device_json_buf[pos++] = (char)c; }
    }

    if (pos < (int)sizeof(self->device_json_buf) - 64) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos,
            "\",\"manufacturer\":\"GE Appliances\"");
    }

    if (self->model_number && self->model_number[0] && pos < (int)sizeof(self->device_json_buf) - 128) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, ",\"model\":\"");
        for (const char* p = self->model_number; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
            else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
            else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
            else { self->device_json_buf[pos++] = (char)c; }
        }
        if (pos < (int)sizeof(self->device_json_buf) - 2) self->device_json_buf[pos++] = '"';
    }

    if (self->serial_number && self->serial_number[0] && pos < (int)sizeof(self->device_json_buf) - 128) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, ",\"serial_number\":\"");
        for (const char* p = self->serial_number; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
            else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
            else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
            else { self->device_json_buf[pos++] = (char)c; }
        }
        if (pos < (int)sizeof(self->device_json_buf) - 2) self->device_json_buf[pos++] = '"';
    }

    if (pos < (int)sizeof(self->device_json_buf) - 2) self->device_json_buf[pos++] = '}';
    self->device_json_buf[pos] = '\0';
}

/* ------------------------------------------------------------------ */
/* Decompression helper                                               */
/* ------------------------------------------------------------------ */

static int chunk_decompress(ha_discovery_manager_t* self, const uint8_t* compressed, size_t compressed_len,
                           uint8_t* output, size_t* output_len)
{
#ifdef USE_ESP_IDF_STUBS
    (void)self; (void)compressed; (void)compressed_len; (void)output; (void)output_len;
    return -1;
#else
    tinfl_init(&self->decomp_state);

    size_t src_size = compressed_len;
    size_t dst_size = *output_len;

    tinfl_status status = tinfl_decompress(
        &self->decomp_state,
        compressed, &src_size,
        output, output, &dst_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if (status != TINFL_STATUS_DONE) {
        return -1;
    }

    *output_len = dst_size;
    return 0;
#endif
}

/* ------------------------------------------------------------------ */
/* Runtime config topic filtering                                     */
/* ------------------------------------------------------------------ */

/* Keywords that mark an entity as internal/diagnostic.
 * If filter_config_topics is enabled and the entity name contains
 * any of these (case-insensitive), the entity is skipped. */
static const char* FILTER_KEYWORDS[] = {
    /* Internal/diagnostic */
    "linux diagnostics",
    "gea interface diagnostic",
    "non-volatile usage warning",
    "reset reason",
    "seconds since last reset",
    "program counter",
    "failed assertion",
    "fault code",
    "configuration hash",
    "schedule hash",
    "sha-256",
    "boot loader version",
    "supported image types",
    "ready to enter boot",
    "engineering revision setup",
    "csm fault data",
    /* Cloud/voice/iot */
    "alexa registration",
    "matter commissioning",
    "onboarding",
    "voice module",
    "push notification",
    /* Allowability/availability metadata */
    "range data",
    "expiration limit",
    "modification available",
    "action available",
    "action availability",
    "supported feature",
    "supported state",
    "supported equipment",
    "supported sound theme",
    "supported notification",
    "supported setting",
    "supported device",
    "requested parameter",
    "request setting",
    "request mask",
    "request configuration",
    /* Time/network */
    "clock time",
    "ntp",
    "time zone",
    "daylight saving",
    "calendar",
    "wifi status",
    "network status",
    "signal strength",
    "ble master",
    "bluetooth master",
    /* Energy/camera */
    "electrical pricing",
    "demand response",
    "time of use pricing",
    "pricing structure",
    "still frame",
    "image upload",
    "camera configuration",
    "camera stream",
    "inference id",
    "cook cam upload",
    /* Sound */
    "available sound",
    "number of sound level",
    /* Enhanced features */
    "enhanced feature",
    "core-enhanced-cloud",
    "request enabled enhanced",
    "current enabled enhanced",
    /* Usage/cycle */
    "usage profile",
    "current report",
    "feature configuration",
    "cycle definition",
    "latched key status",
    "dip switch",
    "most recent cycle status",
    /* Service/diagnostic */
    "service mode",
    NULL
};

/* should_filter_config_topic: converts entity name to lowercase for keyword
 * matching. Uses a 256-byte stack buffer (lower[]). Entity names from the
 * pipeline are bounded by entity_name_buf[160] in ha_discovery_manager_t,
 * so the 256-byte buffer always has headroom. Long-term: replace with a
 * case-insensitive strstr variant to eliminate the stack allocation. */
static bool should_filter_config_topic(const char* name) {
    /* Convert name to lowercase for comparison. */
    char lower[256];
    size_t i, name_len = strlen(name);
    if (name_len >= sizeof(lower)) name_len = sizeof(lower) - 1;
    for (i = 0; i < name_len; i++) {
        lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    }
    lower[name_len] = '\0';

    for (size_t k = 0; FILTER_KEYWORDS[k] != NULL; k++) {
        if (strstr(lower, FILTER_KEYWORDS[k]) != NULL) {
            return true;
        }
    }
    return false;
}

/* Primary-board entries retain the legacy topic. Non-primary entries combine
 * the board address and ERD ID in one MQTT topic segment. */
static void format_erd_topic(char* destination, size_t destination_size,
                             const char* device_id, const char* erd_id,
                             const char* board_address, const char* operation)
{
    if (board_address[0]) {
        snprintf(destination, destination_size,
                 "geappliances/%s/erd/0x%s_0x%s/%s",
                 device_id, board_address, erd_id, operation);
    } else {
        snprintf(destination, destination_size,
                 "geappliances/%s/erd/0x%s/%s", device_id, erd_id, operation);
    }
}

#ifdef HA_DISCOVERY_TEST_EXPORT
void ha_discovery_test_format_erd_topic(char* destination, size_t destination_size,
                                        const char* device_id, const char* erd_id,
                                        const char* board_address, const char* operation)
{
    format_erd_topic(destination, destination_size, device_id, erd_id, board_address, operation);
}
#endif

/* ------------------------------------------------------------------ */
/* Process a single JSONL line: build topic/payload in shared buffers */
/* ------------------------------------------------------------------ */

static bool process_jsonl_line(ha_discovery_manager_t* self, const char* line)
{
    if (self->device_id == NULL) return false;
    const char* val = NULL;
    size_t len = 0;

    /* Required fields */
    if (!json_get_str(line, "i", &val, &len)) return false;
    if (len >= sizeof(self->erd_id_hex_buf)) len = sizeof(self->erd_id_hex_buf) - 1;
    memcpy(self->erd_id_hex_buf, val, len);
    self->erd_id_hex_buf[len] = '\0';
    const char* erd_id_hex = self->erd_id_hex_buf;

    if (!json_get_str(line, "n", &val, &len)) return false;
    json_unescape(val, len, self->entity_name_buf, sizeof(self->entity_name_buf));

    /* Runtime config topic filtering. */
    if (self->filter_config_topics && should_filter_config_topic(self->entity_name_buf)) {
        self->total_filtered++;
        return false;
    }

    if (!json_get_str(line, "d", &val, &len)) return false;
    json_unescape(val, len, self->domain_buf, sizeof(self->domain_buf));

    /* Optional fields — use struct buffers to avoid stack overflow. */
    self->field_id_buf[0] = '\0';
    self->board_address_buf[0] = '\0';
    self->paired_erd_buf[0] = '\0';
    self->role_buf[0] = '\0';
    self->unit_buf[0] = '\0';
    self->device_class_buf[0] = '\0';
    self->state_class_buf[0] = '\0';
    self->options_buf[0] = '\0';
    self->data_type_buf[0] = '\0';
    self->mode_buf[0] = '\0';
    self->payload_on_buf[0] = '\0';
    self->payload_off_buf[0] = '\0';
    self->state_on_buf[0] = '\0';
    self->state_off_buf[0] = '\0';
    self->min_buf[0] = '\0';
    self->max_buf[0] = '\0';
    self->step_buf[0] = '\0';

    if (json_get_str(line, "fi", &val, &len)) json_unescape(val, len, self->field_id_buf, sizeof(self->field_id_buf));
    if (json_get_str(line, "a", &val, &len)) json_unescape(val, len, self->board_address_buf, sizeof(self->board_address_buf));
    if (json_get_str(line, "p", &val, &len)) json_unescape(val, len, self->paired_erd_buf, sizeof(self->paired_erd_buf));
    if (json_get_str(line, "r", &val, &len)) json_unescape(val, len, self->role_buf, sizeof(self->role_buf));
    if (json_get_str(line, "u", &val, &len)) json_unescape(val, len, self->unit_buf, sizeof(self->unit_buf));
    if (json_get_str(line, "dc", &val, &len)) json_unescape(val, len, self->device_class_buf, sizeof(self->device_class_buf));
    if (json_get_str(line, "sc", &val, &len)) json_unescape(val, len, self->state_class_buf, sizeof(self->state_class_buf));
    if (json_get_str(line, "o", &val, &len)) json_unescape(val, len, self->options_buf, sizeof(self->options_buf));
    if (json_get_str(line, "dt", &val, &len)) json_unescape(val, len, self->data_type_buf, sizeof(self->data_type_buf));
    if (json_get_str(line, "sf", &val, &len)) json_unescape(val, len, self->scale_factor_buf, sizeof(self->scale_factor_buf));
    if (json_get_str(line, "m", &val, &len)) json_unescape(val, len, self->mode_buf, sizeof(self->mode_buf));
    if (json_get_str(line, "pon", &val, &len)) json_unescape(val, len, self->payload_on_buf, sizeof(self->payload_on_buf));
    if (json_get_str(line, "poff", &val, &len)) json_unescape(val, len, self->payload_off_buf, sizeof(self->payload_off_buf));
    if (json_get_str(line, "son", &val, &len)) json_unescape(val, len, self->state_on_buf, sizeof(self->state_on_buf));
    if (json_get_str(line, "soff", &val, &len)) json_unescape(val, len, self->state_off_buf, sizeof(self->state_off_buf));
    if (json_get_str(line, "mn", &val, &len)) json_unescape(val, len, self->min_buf, sizeof(self->min_buf));
    if (json_get_str(line, "mx", &val, &len)) json_unescape(val, len, self->max_buf, sizeof(self->max_buf));
    if (json_get_str(line, "st", &val, &len)) json_unescape(val, len, self->step_buf, sizeof(self->step_buf));

    /* Board addresses are emitted as two hexadecimal digits by the custom
     * profile generator. Reject malformed optional data before it reaches a
     * discovery topic or entity identifier. */
    if (self->board_address_buf[0] &&
        (strlen(self->board_address_buf) != 2 ||
         strspn(self->board_address_buf, "0123456789abcdefABCDEF") != 2)) {
        self->total_filtered++;
        return false;
    }

    uint16_t erd_id = (uint16_t)strtoul(erd_id_hex, NULL, 16);

    /* Check if ERD is registered (binary search). */
    if (!erd_is_registered_sorted(self, erd_id)) {
        self->total_filtered++;
        return false;
    }

    /* Check paired ERD if present. */
    if (self->paired_erd_buf[0]) {
        uint16_t paired_id = (uint16_t)strtoul(self->paired_erd_buf, NULL, 16);
        if (!erd_is_registered_sorted(self, paired_id)) {
            self->total_filtered++;
            return false;
        }
    }

    /* Build unique_id. Address-aware entries include the two-digit board
     * address, keeping overlapping ERD IDs on separate boards distinct. */
    if (self->field_id_buf[0] && self->board_address_buf[0]) {
        snprintf(self->unique_id_buf, sizeof(self->unique_id_buf), "%s_erd_%s_address_%s_%s",
                 self->device_id, erd_id_hex, self->board_address_buf, self->field_id_buf);
    } else if (self->field_id_buf[0]) {
        snprintf(self->unique_id_buf, sizeof(self->unique_id_buf), "%s_erd_%s_%s", self->device_id, erd_id_hex, self->field_id_buf);
    } else if (self->board_address_buf[0]) {
        snprintf(self->unique_id_buf, sizeof(self->unique_id_buf), "%s_erd_%s_address_%s",
                 self->device_id, erd_id_hex, self->board_address_buf);
    } else {
        snprintf(self->unique_id_buf, sizeof(self->unique_id_buf), "%s_erd_%s", self->device_id, erd_id_hex);
    }

    /* Build state_topic and command_topic */
    format_erd_topic(self->state_topic_buf, sizeof(self->state_topic_buf), self->device_id,
                     erd_id_hex, self->board_address_buf, "value");
    format_erd_topic(self->command_topic_buf, sizeof(self->command_topic_buf), self->device_id,
                     erd_id_hex, self->board_address_buf, "write");

    /* For paired entities, swap state/command topics */
    if (self->paired_erd_buf[0]) {
        if (self->role_buf[0] && strcmp(self->role_buf, "request") == 0) {
            format_erd_topic(self->actual_command_topic_buf, sizeof(self->actual_command_topic_buf),
                             self->device_id, erd_id_hex, self->board_address_buf, "write");
            format_erd_topic(self->actual_state_topic_buf, sizeof(self->actual_state_topic_buf),
                             self->device_id, self->paired_erd_buf, self->board_address_buf, "value");
        } else {
            format_erd_topic(self->actual_state_topic_buf, sizeof(self->actual_state_topic_buf),
                             self->device_id, erd_id_hex, self->board_address_buf, "value");
            format_erd_topic(self->actual_command_topic_buf, sizeof(self->actual_command_topic_buf),
                             self->device_id, self->paired_erd_buf, self->board_address_buf, "write");
        }
    } else {
        snprintf(self->actual_state_topic_buf, sizeof(self->actual_state_topic_buf), "%s", self->state_topic_buf);
        snprintf(self->actual_command_topic_buf, sizeof(self->actual_command_topic_buf), "%s", self->command_topic_buf);
    }

    /* Build topic using pre-computed domain prefix if available.
     * domain_topic_prefix is 128 bytes. Worst case: "homeassistant/"(14) +
     * domain_buf[32] + "/"(1) + device_id[64] + "/"(1) = 112 bytes.
     * Address-aware custom entries add "_address_xx" to the suffix. Truncation
     * is detected below so oversized configuration topics are safely skipped. */
    if (self->domain_topic_prefix[0] == '\0' || strcmp(self->domain_buf, self->current_domain_prefix_buf) != 0) {
        /* Domain changed or first use — rebuild prefix. */
        snprintf(self->domain_topic_prefix, sizeof(self->domain_topic_prefix),
            "homeassistant/%s/%s/", self->domain_buf, self->device_id);
        /* Detect prefix truncation: if it doesn't end with '/', it was cut short. */
        size_t plen = strlen(self->domain_topic_prefix);
        if (plen == 0 || self->domain_topic_prefix[plen - 1] != '/') {
            self->total_filtered++;
            return false;
        }
        strncpy(self->current_domain_prefix_buf, self->domain_buf, sizeof(self->current_domain_prefix_buf) - 1);
        self->current_domain_prefix_buf[sizeof(self->current_domain_prefix_buf) - 1] = '\0';
    }
    {
        size_t prefix_len = strlen(self->domain_topic_prefix);
        if (prefix_len >= sizeof(self->topic_buf)) {
            self->total_filtered++;
            return false;
        }
        /* Copy prefix first so strlen() below reads initialized memory. */
        memcpy(self->topic_buf, self->domain_topic_prefix, prefix_len);
        size_t remaining = sizeof(self->topic_buf) - prefix_len - 1; /* -1 for null */
        if (self->field_id_buf[0] && self->board_address_buf[0]) {
            snprintf(self->topic_buf + prefix_len, remaining, "%s_address_%s_%s/config",
                     erd_id_hex, self->board_address_buf, self->field_id_buf);
        } else if (self->field_id_buf[0]) {
            snprintf(self->topic_buf + prefix_len, remaining, "%s_%s/config", erd_id_hex, self->field_id_buf);
        } else if (self->board_address_buf[0]) {
            snprintf(self->topic_buf + prefix_len, remaining, "%s_address_%s/config",
                     erd_id_hex, self->board_address_buf);
        } else {
            snprintf(self->topic_buf + prefix_len, remaining, "%s/config", erd_id_hex);
        }
        /* Detect suffix truncation: if topic doesn't end with '/config', it was cut short. */
        size_t topic_len = strlen(self->topic_buf);
        if (topic_len < 7 || strcmp(self->topic_buf + topic_len - 7, "/config") != 0) {
            self->total_filtered++;
            return false;
        }
    }

    /* Build payload directly in shared buffer.
     * Templates are embedded directly from the raw JSONL line with re-escaping,
     * avoiding intermediate buffer limits. */
    char* payload = self->payload_buf;
    int pos = 0;
    int space = (int)sizeof(self->payload_buf) - 1;  /* leave room for null */

    int n;

    /* Button domain: simpler payload, no state_topic/value_template. */
    if (strcmp(self->domain_buf, "button") == 0) {
        n = snprintf(payload + pos, space,
            "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device\":%s,",
            self->entity_name_buf, self->unique_id_buf, self->device_json_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        n = snprintf(payload + pos, space,
            "\"command_topic\":\"%s\",\"payload_press\":\"1\",",
            self->actual_command_topic_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        if (self->device_class_buf[0]) {
            n = snprintf(payload + pos, space, "\"device_class\":\"%s\",", self->device_class_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
    } else {
        /* Non-button domains: sensor, binary_sensor, switch, select, number, etc. */
        n = snprintf(payload + pos, space,
            "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device\":%s,",
            self->entity_name_buf, self->unique_id_buf, self->device_json_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        n = snprintf(payload + pos, space,
            "\"state_topic\":\"%s\",", self->actual_state_topic_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        /* Embed value_template directly from raw JSONL with re-escaping. */
        if (json_get_str(line, "vt", &val, &len)) {
            n = snprintf(payload + pos, space, "\"value_template\":\"");
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
            int reescaped = json_embed_value(val, len, payload + pos, space);
            if (reescaped >= space) goto too_large;
            pos += reescaped; space -= reescaped;
            n = snprintf(payload + pos, space, "\",");
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }

        /* Embed command_template directly from raw JSONL with re-escaping. */
        if (json_get_str(line, "ct", &val, &len)) {
            n = snprintf(payload + pos, space, "\"command_topic\":\"%s\",\"command_template\":\"", self->actual_command_topic_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
            int reescaped = json_embed_value(val, len, payload + pos, space);
            if (reescaped >= space) goto too_large;
            pos += reescaped; space -= reescaped;
            n = snprintf(payload + pos, space, "\",");
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        } else if (self->paired_erd_buf[0]) {
            n = snprintf(payload + pos, space, "\"command_topic\":\"%s\",", self->actual_command_topic_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }

        if (self->unit_buf[0]) {
            n = snprintf(payload + pos, space, "\"unit_of_measurement\":\"%s\",", self->unit_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->device_class_buf[0]) {
            n = snprintf(payload + pos, space, "\"device_class\":\"%s\",", self->device_class_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->state_class_buf[0]) {
            n = snprintf(payload + pos, space, "\"state_class\":\"%s\",", self->state_class_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->options_buf[0]) {
            n = snprintf(payload + pos, space, "\"options\":%s,", self->options_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        /* Number domain: use mn/mx/st from JSONL (scaled values). */
        if (strcmp(self->domain_buf, "number") == 0) {
            if (self->min_buf[0]) {
                n = snprintf(payload + pos, space, "\"min\":%s,", self->min_buf);
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
            if (self->max_buf[0]) {
                n = snprintf(payload + pos, space, "\"max\":%s,", self->max_buf);
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
            if (self->step_buf[0]) {
                n = snprintf(payload + pos, space, "\"step\":%s,", self->step_buf);
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
            /* Fallback to dt-based ranges if mn/mx not set. */
            if (!self->min_buf[0] && self->data_type_buf[0]) {
                if (strcmp(self->data_type_buf, "u8") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":0,\"max\":255,");
                } else if (strcmp(self->data_type_buf, "i8") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":-128,\"max\":127,");
                } else if (strcmp(self->data_type_buf, "u16") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":0,\"max\":65535,");
                } else if (strcmp(self->data_type_buf, "i16") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":-32768,\"max\":32767,");
                } else if (strcmp(self->data_type_buf, "u32") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":0,\"max\":4294967295,");
                } else if (strcmp(self->data_type_buf, "i32") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":-2147483648,\"max\":2147483647,");
                }
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
        }
        /* Fallback step from scale_factor if st not set (for non-number domains). */
        if (self->scale_factor_buf[0] && !self->step_buf[0] && strcmp(self->domain_buf, "number") == 0) {
            n = snprintf(payload + pos, space, "\"step\":%s,", self->scale_factor_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->mode_buf[0]) {
            n = snprintf(payload + pos, space, "\"mode\":\"%s\",", self->mode_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->payload_on_buf[0]) {
            n = snprintf(payload + pos, space, "\"payload_on\":\"%s\",", self->payload_on_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->payload_off_buf[0]) {
            n = snprintf(payload + pos, space, "\"payload_off\":\"%s\",", self->payload_off_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->state_on_buf[0]) {
            n = snprintf(payload + pos, space, "\"state_on\":\"%s\",", self->state_on_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->state_off_buf[0]) {
            n = snprintf(payload + pos, space, "\"state_off\":\"%s\",", self->state_off_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
    }

    /* Remove trailing comma and close */
    if (pos > 0 && payload[pos - 1] == ',') {
        payload[pos - 1] = '\0';
        pos--; space++;
    }
    n = snprintf(payload + pos, space, "}");
    if (n < 0 || n >= space) goto too_large;
    pos += n;
    payload[pos] = '\0';

    return true;

too_large:
    ESP_LOGW(TAG, "Payload too large for ERD 0x%s, skipping", erd_id_hex);
    self->total_filtered++;
    return false;
}

/* ------------------------------------------------------------------ */
/* Category filtering by appliance type                               */
/* ------------------------------------------------------------------ */

static bool should_process_category(const char* category, uint8_t appliance_type)
{
    if (strcmp(category, "common") == 0) return true;

    /* Appliance type enum (ERD 0x0008):
     * 0=WaterHeater, 1=ClothesDryer, 2=ClothesWasher, 3=Refrigerator,
     * 4=Microwave, 5=Advantium, 6=Dishwasher, 7=Oven, 8=ElectricRange,
     * 9=GasRange, 10=ThermostatRAC, 11=ElectricCooktop, 12=PizzaOven,
     * 13=GasCooktop, 14=SplitDFSDuctFreeSplitAC, 15=Hood,
     * 16=PointOfEntryWaterFilter, 17=InductionCooktop, 18=DeliveryBox,
     * 19=KitchenHubVentHood, 20=ZonelinePTAC, 21=WaterSoftener,
     * 22=PortableAC, 23=CombinationWasherDryer, 24=DualZoneWineChiller,
     * 25=BeverageCenter, 26=CoffeeBrewer, 27=OpalNuggetIceMaker,
     * 28=InHomeGrower, 29=Dehumidifer, 30=UnderCounterIceMaker,
     * 31=ThroughWallAC, 32=FPDishDrawer, 33=EspressoCoffeeMaker,
     * 34=ToasterOven, 35=ZonelineVertical, 36=CentralDFSDuctFreeSplitController,
     * 37=BLEMeshGateway, 38=StandMixer, 39=FPCooktop,
     * 40=FPCooktopTeppanyaki, 41=FPVentilationDowndraft, 42=SmartPlug,
     * 43=Smoker, 44=AirHandlerVRF, 45=FabricCareCabinetCloset,
     * 46=LaundryCenter, 47=Grill, 48=Freezer, 49=WarmingDrawer,
     * 50=VacuumSealDrawer, 51=WineCabinet, 52=CentralAC, 53=SoftStarter,
     * 54=HearthPizzaOven, 55=SourdoughStarter, 56=Thermostat
     */

    /* Dishwasher: 6=Dishwasher, 32=FPDishDrawer */
    if (appliance_type == 6 || appliance_type == 32) {
        if (strcmp(category, "dishwasher") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Refrigeration: 3=Refrigerator, 24=DualZoneWineChiller,
     * 25=BeverageCenter, 48=Freezer, 51=WineCabinet */
    if (appliance_type == 3 || appliance_type == 24 ||
        appliance_type == 25 || appliance_type == 48 ||
        appliance_type == 51) {
        if (strcmp(category, "refrigeration") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Laundry: 1=ClothesDryer, 2=ClothesWasher, 23=CombinationWasherDryer,
     * 45=FabricCareCabinetCloset, 46=LaundryCenter */
    if (appliance_type == 1 || appliance_type == 2 ||
        appliance_type == 23 || appliance_type == 45 ||
        appliance_type == 46) {
        if (strcmp(category, "laundry") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Range/Cooking: 4=Microwave, 5=Advantium, 7=Oven, 8=ElectricRange,
     * 9=GasRange, 11=ElectricCooktop, 12=PizzaOven, 13=GasCooktop,
     * 15=Hood, 17=InductionCooktop, 19=KitchenHubVentHood,
     * 34=ToasterOven, 39=FPCooktop, 40=FPCooktopTeppanyaki,
     * 41=FPVentilationDowndraft, 43=Smoker, 47=Grill,
     * 49=WarmingDrawer, 54=HearthPizzaOven */
    if (appliance_type == 4 || appliance_type == 5 ||
        appliance_type == 7 || appliance_type == 8 ||
        appliance_type == 9 || appliance_type == 11 ||
        appliance_type == 12 || appliance_type == 13 ||
        appliance_type == 15 || appliance_type == 17 ||
        appliance_type == 19 || appliance_type == 34 ||
        appliance_type == 39 || appliance_type == 40 ||
        appliance_type == 41 || appliance_type == 43 ||
        appliance_type == 47 || appliance_type == 49 ||
        appliance_type == 54) {
        if (strcmp(category, "range") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Air conditioning: 10=ThermostatRAC, 14=SplitDFSDuctFreeSplitAC,
     * 20=ZonelinePTAC, 22=PortableAC,
     * 31=ThroughWallAC, 35=ZonelineVertical, 36=CentralDFSDuctFreeSplitController,
     * 44=AirHandlerVRF, 52=CentralAC, 56=Thermostat */
    if (appliance_type == 10 || appliance_type == 14 ||
        appliance_type == 20 || appliance_type == 22 ||
        appliance_type == 31 || appliance_type == 35 ||
        appliance_type == 36 || appliance_type == 44 ||
        appliance_type == 52 ||
        appliance_type == 56) {
        if (strcmp(category, "airconditioning") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Water heater: 0=WaterHeater */
    if (appliance_type == 0) {
        if (strcmp(category, "waterheater") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Water filter: 16=PointOfEntryWaterFilter, 21=WaterSoftener */
    if (appliance_type == 16 || appliance_type == 21) {
        if (strcmp(category, "waterfilter") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Small appliance: 18=DeliveryBox, 26=CoffeeBrewer, 27=OpalNuggetIceMaker,
     * 28=InHomeGrower, 29=Dehumidifer, 30=UnderCounterIceMaker, 33=EspressoCoffeeMaker,
     * 37=BLEMeshGateway, 38=StandMixer, 42=SmartPlug, 50=VacuumSealDrawer,
     * 53=SoftStarter, 55=SourdoughStarter */
    if (appliance_type == 18 || appliance_type == 26 ||
        appliance_type == 27 || appliance_type == 28 ||
        appliance_type == 29 || appliance_type == 30 ||
        appliance_type == 33 || appliance_type == 37 ||
        appliance_type == 38 ||
        appliance_type == 42 || appliance_type == 50 ||
        appliance_type == 53 || appliance_type == 55) {
        if (strcmp(category, "smallappliance") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Cleanup helper                                                     */
/* ------------------------------------------------------------------ */

static void cleanup_resources(ha_discovery_manager_t* self)
{
    ha_discovery_cleanup_destroy(&self->cleanup);
}

static uint16_t discovery_category_count(const ha_discovery_manager_t* self)
{
    return ha_discovery_category_count + (self->custom_data ? 1 : 0);
}

static ha_discovery_category_t discovery_category_at(const ha_discovery_manager_t* self, uint16_t index)
{
    if (index < ha_discovery_category_count) return ha_discovery_categories[index];
    return { "custom", self->custom_data,
      reinterpret_cast<const ha_discovery_chunk_t*>(self->custom_chunks),
      self->custom_num_chunks, self->custom_max_decompressed_chunk };
}

/* ------------------------------------------------------------------ */
/* run(): publish one entity per call                                 */
/* ------------------------------------------------------------------ */

void ha_discovery_manager_run(ha_discovery_manager_t* self)
{
    if (self->state != ha_discovery_state_building &&
        self->state != ha_discovery_state_discovering) {
        return;
    }

    /* If still in BUILDING state, the build happened inline in start().
     * Transition to DISCOVERING on the first run() call. */
    if (self->state == ha_discovery_state_building) {
        self->state = ha_discovery_state_discovering;
        self->current_category = 0;
        self->current_chunk = 0;
        self->current_offset = 0;
        self->current_decomp_size = 0;
        self->current_domain_prefix_buf[0] = '\0';
        ESP_LOGI(TAG, "Generating MQTT discovery payloads (filtering: %s)",
            self->filter_config_topics ? "enabled" : "disabled");
        return;
    }

    /* Discovering state: decompress chunks and publish one entity per call. */
    while (self->state == ha_discovery_state_discovering) {
        /* Find the next category to process. */
        while (self->current_category < discovery_category_count(self)) {
            ha_discovery_category_t cat = discovery_category_at(self, self->current_category);

            if (strcmp(cat.name, "custom") != 0 && !should_process_category(cat.name, self->appliance_type)) {
                /* Skip unneeded category. Return to main loop; next run()
                 * will try the next category. */
                self->current_category++;
                self->current_chunk = 0;
                self->current_offset = 0;
                self->current_decomp_size = 0;
                return;
            }

            /* Decompress the current chunk if needed. */
            if (self->current_decomp_size == 0) {
                if (self->current_chunk >= cat.num_chunks) {
                    /* Done with this category. Return to main loop; next
                     * run() will try the next category. */
                    self->current_category++;
                    self->current_chunk = 0;
                    self->current_offset = 0;
                    self->current_decomp_size = 0;
                    return;
                }

                const ha_discovery_chunk_t* chunk = &cat.chunks[self->current_chunk];
                ESP_LOGD(TAG, "Decompressing chunk %u/%u for category '%s' (compressed %u bytes)",
                    self->current_chunk, cat.num_chunks, cat.name, chunk->size);
                const uint8_t* src = cat.data + chunk->offset;

                size_t dst_size = sizeof(self->decomp_buf);
                if (chunk_decompress(self, src, chunk->size, self->decomp_buf, &dst_size) != 0) {
                    ESP_LOGE(TAG, "Decompression failed for category '%s' chunk %u (offset %lu, size %u)",
                        cat.name, self->current_chunk, (unsigned long)chunk->offset, chunk->size);
                    self->state = ha_discovery_state_failed;
                    return;
                }
                self->current_decomp_size = (uint32_t)dst_size;
                self->current_offset = 0;
            }

            /* Parse lines from the current decompressed chunk. */
            const char* decomp = (const char*)self->decomp_buf;

            while (self->current_offset < self->current_decomp_size) {
                /* Find the next line. */
                const char* line_start = decomp + self->current_offset;
                const char* line_end = line_start;
                while ((uintptr_t)(line_end - decomp) < self->current_decomp_size &&
                       *line_end != '\n' && *line_end != '\r') {
                    line_end++;
                }

                size_t line_len = (size_t)(line_end - line_start);
                if (line_len == 0) {
                    self->current_offset++;
                    continue;
                }
                if (line_len >= sizeof(self->line_buf) - 1) {
                    line_len = sizeof(self->line_buf) - 1;
                }
                memcpy(self->line_buf, line_start, line_len);
                self->line_buf[line_len] = '\0';

                /* Process the line. */
                if (process_jsonl_line(self, self->line_buf)) {
                    /* Publish. */
                    if (!self->mqtt_client) {
                        /* No MQTT client yet — skip this entity.
                         * Discovery will be retried later when MQTT connects. */
                        self->current_offset = (uint32_t)(line_end - decomp) + 1;
                        return;
                    }
                    if (!mqtt_client_publish_raw(self->mqtt_client, self->topic_buf,
                        self->payload_buf, strlen(self->payload_buf), true)) {
                        /* Publish dropped (queue full) — don't advance offset
                         * so the entity is retried on the next run() call. */
                        return;
                    }
                    self->total_published++;
                    self->total_discovered++;

                    /* Advance offset past this line after successful publish. */
                    self->current_offset = (uint32_t)(line_end - decomp) + 1;

                    ESP_LOGD(TAG, "Published: %s (0x%s)", self->entity_name_buf, self->erd_id_hex_buf);

                    /* Log category progress periodically. */
                    if (self->total_published % 50 == 0) {
                        ESP_LOGI(TAG, "Category %s: %lu discovered, %lu published",
                            cat.name, (unsigned long)self->total_discovered, (unsigned long)self->total_published);
                    }

                    /* One entity per call — return to main loop. */
                    return;
                } else {
                    /* Line was filtered; advance offset past it. */
                    self->current_offset = (uint32_t)(line_end - decomp) + 1;
                }
            }
            /* Done with this chunk. Return to main loop; next run() will
             * advance to the next chunk or category. This avoids blocking
             * the main loop while skipping empty chunks. */
            self->current_chunk++;
            self->current_offset = 0;
            self->current_decomp_size = 0;
            return;
        }

        /* Check if all categories are done. */
        if (self->current_category >= discovery_category_count(self)) {
            self->state = ha_discovery_state_complete;
            break;
        }
    }

    /* If just completed, do cleanup and logging on this call.
     * This is separate from the publish-return path so the last
     * entity publish doesn't block on cleanup. */
    if (self->state == ha_discovery_state_complete) {
        cleanup_resources(self);

        size_t free_heap __attribute__((unused)) = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_free __attribute__((unused)) = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "HA discovery complete: %lu published, %lu filtered",
            (unsigned long)self->total_published, (unsigned long)self->total_filtered);
        ESP_LOGV(TAG, "Heap after discovery: free=%u, largest_block=%u, fragmentation=%.1f%%",
            (unsigned)free_heap, (unsigned)largest_free,
            (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ha_discovery_manager_init(ha_discovery_manager_t* self)
{
    /* Preserve custom data pointers; they are set by codegen and survive reinit. */
    const uint8_t* custom_data = self->custom_data;
    const void* custom_chunks = self->custom_chunks;
    uint16_t custom_num_chunks = self->custom_num_chunks;
    uint16_t custom_max_chunk = self->custom_max_decompressed_chunk;
    uint32_t custom_data_hash = self->custom_data_hash;

    /* Zero all runtime state. */
    memset(self, 0, sizeof(*self));

    /* Restore custom data pointers. */
    self->custom_data = custom_data;
    self->custom_chunks = custom_chunks;
    self->custom_num_chunks = custom_num_chunks;
    self->custom_max_decompressed_chunk = custom_max_chunk;
    self->custom_data_hash = custom_data_hash;

    self->state = ha_discovery_state_idle;
    ha_discovery_cleanup_init(&self->cleanup);
}
void ha_discovery_manager_set_custom_data(ha_discovery_manager_t* self,
    const uint8_t* data, const void* chunks, uint16_t num_chunks, uint16_t max_chunk, uint32_t data_hash)
{
    self->custom_data = data;
    self->custom_chunks = chunks;
    self->custom_num_chunks = num_chunks;
    self->custom_max_decompressed_chunk = max_chunk;
    self->custom_data_hash = data_hash;
}

void ha_discovery_manager_configure(
    ha_discovery_manager_t* self,
    const char* device_id,
    const char* model_number,
    const char* serial_number,
    uint8_t appliance_type,
    bool filter_config_topics,
    erd_cache_t* cache,
    i_mqtt_client_t* mqtt_client)
{
    self->device_id = device_id;
    self->model_number = model_number;
    self->serial_number = serial_number;
    self->appliance_type = appliance_type;
    self->filter_config_topics = filter_config_topics;
    self->cache = cache;
    self->mqtt_client = mqtt_client;
}

void ha_discovery_manager_start(ha_discovery_manager_t* self)
{
    if (self->state != ha_discovery_state_idle) return;

    /* Build sorted ERD list and device JSON inline. */
    build_sorted_erd_list(self);
    build_device_json(self);

    self->state = ha_discovery_state_building;
}

void ha_discovery_manager_cleanup(ha_discovery_manager_t* self)
{
    cleanup_resources(self);

    memset(self, 0, sizeof(*self));
}


bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self)
{
    return self->state == ha_discovery_state_building ||
           self->state == ha_discovery_state_discovering;
}

ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self)
{
    return self->state;
}
