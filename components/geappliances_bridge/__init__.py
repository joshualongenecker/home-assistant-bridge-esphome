"""ESPHome component for GE Appliances Bridge."""
from __future__ import annotations

import logging
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, sensor, uart
from esphome.const import CONF_ID, CONF_STATE_CLASS
from esphome.core import CORE, EnumValue, ID

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@joshualongenecker"]
DEPENDENCIES = ["uart", "mqtt"]
AUTO_LOAD = ["sensor", "button"]

# UART configuration keys
CONF_GEA3_UART_ID = "gea3_uart_id"
CONF_GEA2_UART_ID = "gea2_uart_id"

# Bridge (MQTT) configuration keys
CONF_ADAPTER_ADDRESS = "adapter_address"
CONF_DEVICE_ID = "device_id"
CONF_MODE = "mode"
CONF_POLLING_INTERVAL = "polling_interval"
CONF_POLLING_ONLY_PUBLISH_ON_CHANGE = "polling_onlypublish_onchange"
CONF_APPLIANCE_API_PARSING = "appliance_api_parsing"
CONF_CUSTOM_ERDS = "custom_erds"
CONF_GENERATE_DEVICE_CONFIG = "generate_device_config"
CONF_ERD_PUBLISH_RATE_SENSOR = "erd_publish_rate_sensor"
CONF_ERD_CACHE_ENTRIES_SENSOR = "erd_cache_entries_sensor"
CONF_ERD_CACHE_UPDATES_SENSOR = "erd_cache_updates_sensor"
CONF_MQTT_PUBLISH_RATE_SENSOR = "mqtt_publish_rate_sensor"
CONF_MQTT_DISCONNECT_COUNT_SENSOR = "mqtt_disconnect_count_sensor"
CONF_MQTT_DISCONNECT_DURATION_SENSOR = "mqtt_disconnect_duration_sensor"
CONF_THROTTLE_RATE_SECONDS = "throttle_rate_seconds"
CONF_FILTER_CONFIG_TOPICS = "filter_config_topics"
CONF_DISCOVERY_REFRESH_BUTTON = "discovery_refresh_button"



# Bridge mode options (polling vs subscriptions)
MODE_POLL = "poll"
MODE_SUBSCRIBE = "subscribe"
MODE_AUTO = "auto"

# Mode enum values (must match BridgeMode enum in C++)
MODE_POLL_VALUE = 0
MODE_SUBSCRIBE_VALUE = 1
MODE_AUTO_VALUE = 2

geappliances_bridge_ns = cg.esphome_ns.namespace("geappliances_bridge")
GeappliancesBridge = geappliances_bridge_ns.class_(
    "GeappliancesBridge", cg.Component
)

# Concrete button subclass for discovery refresh (Button is abstract)
DiscoveryRefreshButton = geappliances_bridge_ns.class_(
    "DiscoveryRefreshButton", button.Button
)


async def _create_diagnostic_sensor(config: dict[str, Any], config_key: str, default_name: str, sensor_id: str, state_class: str, var: Any, setter_name: str, extra: dict[str, Any] | None = None) -> None:
    """Create a diagnostic sensor if enabled in config.
    
    Args:
        config: The full configuration dictionary.
        config_key: The config key to check for the sensor setting.
        default_name: Default display name for the sensor.
        sensor_id: The ID slug for the sensor (used for CONF_ID).
        state_class: Home Assistant state class (e.g. "measurement", "total_increasing").
        var: The GeappliancesBridge variable to set the sensor on.
        setter_name: Name of the setter method on var (e.g. "set_erd_publish_rate_sensor").
        extra: Optional extra sensor config keys (e.g. {"accuracy_decimals": 0}).
    """
    val = config.get(config_key, True)
    if val is not False:
        if val is True:
            val = {
                "name": default_name,
                CONF_ID: ID(sensor_id, is_declaration=True, type=sensor.Sensor),
                CONF_STATE_CLASS: _make_state_class(state_class),
                "disabled_by_default": False,
                "force_update": False,
            }
            if extra:
                val.update(extra)
        sens = await sensor.new_sensor(val)
        cg.add(getattr(var, setter_name)(sens))


def _make_state_class(value: str):
    """Wrap a state_class string as an EnumValue for codegen."""
    v = cv.add_class_to_obj(value, EnumValue)
    v.enum_value = sensor.STATE_CLASSES[value]
    return v




def validate_at_least_one_uart(config: dict[str, Any]) -> dict[str, Any]:
    """Validate that at least one UART ID is specified.

    Args:
        config: Configuration dictionary

    Returns:
        The validated configuration

    Raises:
        cv.Invalid: If neither gea3_uart_id nor gea2_uart_id is specified
    """
    if CONF_GEA3_UART_ID not in config and CONF_GEA2_UART_ID not in config:
        raise cv.Invalid("At least one of gea3_uart_id or gea2_uart_id must be specified")
    return config


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.Optional(CONF_GEA3_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_GEA2_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_ADAPTER_ADDRESS, default=0xE4): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_DEVICE_ID): cv.All(cv.string, cv.Length(max=91)),
        cv.Optional(CONF_MODE, default=MODE_AUTO): cv.enum(
            {
                MODE_POLL: MODE_POLL_VALUE,
                MODE_SUBSCRIBE: MODE_SUBSCRIBE_VALUE,
                MODE_AUTO: MODE_AUTO_VALUE,
            },
            upper=False
        ),
        cv.Optional(CONF_POLLING_INTERVAL, default=10000): cv.positive_int,
        cv.Optional(CONF_POLLING_ONLY_PUBLISH_ON_CHANGE): cv.boolean,
        cv.Optional(CONF_APPLIANCE_API_PARSING, default=True): cv.boolean,
        cv.Optional(CONF_GENERATE_DEVICE_CONFIG, default=True): cv.boolean,
        cv.Optional(CONF_CUSTOM_ERDS, default=[]): cv.All(cv.ensure_list(
            cv.int_range(min=0, max=0xFFFF)
        ), cv.Length(max=128)),
        cv.Optional(CONF_THROTTLE_RATE_SECONDS, default=1): cv.int_range(min=0, max=255),
        cv.Optional(CONF_ERD_PUBLISH_RATE_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="ERD Publish Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_ERD_CACHE_ENTRIES_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema().extend(cv.Schema({
                cv.Optional("name", default="ERD Cache Entries"): cv.string,
            })),
        ),
        cv.Optional(CONF_ERD_CACHE_UPDATES_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="ERD Cache Update Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_PUBLISH_RATE_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="MQTT Publish Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_DISCONNECT_COUNT_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="total_increasing").extend(cv.Schema({
                cv.Optional("name", default="MQTT Disconnect Count"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_DISCONNECT_DURATION_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="MQTT Last Disconnect Duration"): cv.string,
            })),
        ),
        cv.Optional(CONF_FILTER_CONFIG_TOPICS, default=True): cv.boolean,
        cv.Optional(CONF_DISCOVERY_REFRESH_BUTTON, default=True): cv.Any(
            cv.boolean,
            button.button_schema(DiscoveryRefreshButton).extend(cv.Schema({
                cv.Optional("name", default="Discovery Refresh"): cv.string,
            })),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_at_least_one_uart)


async def to_code(config: dict[str, Any]) -> None:
    """Generate C++ code for the component.

    Args:
        config: Configuration dictionary
    """
    # Add library dependencies (pinned to specific commit SHAs to prevent
    # silent breakage from upstream branch movement)
    # tiny-gea-api transitively depends on ryanplusplus/tiny (^7.1.3).
    # Pinning tiny-gea-api to a commit SHA is sufficient — adding tiny
    # directly creates a duplicate graph node ("ryanplusplus/tiny" vs "tiny")
    # that triggers a deduplication warning in ESPHome 2026.x.
    cg.add_library("tiny-gea-api", None, "https://github.com/geappliances/tiny-gea-api#4fa8fee8297e24baa91bfe4a464088a73e7c6a5a")
    # Force async MQTT sends on ESP32 to prevent TWDT crashes.
    # ESPHome defaults idf_send_async to False for ESP32, which means
    # esp_mqtt_client_publish() is synchronous and can block for seconds
    # (network timeout, send buffer full, message fragmentation).
    # On single-core ESP32 variants (C3, C6, S3), a blocking publish from
    # the background mqtt_publisher_task stalls the MQTT library's internal
    # state machine, preventing the main task from running and feeding the
    # Task Watchdog Timer — resulting in a TWDT reset.
    # The enqueue path (USE_MQTT_IDF_ENQUEUE) uses a lock-free queue and a
    # dedicated background task to drain publishes, making publish() non-blocking.
    if CORE.is_esp32:
        cg.add_define("USE_MQTT_IDF_ENQUEUE")
    
    var = cg.new_Pvariable(config[CONF_ID])
    # Deprecation warning for polling_onlypublish_onchange
    if CONF_POLLING_ONLY_PUBLISH_ON_CHANGE in config:
        _LOGGER.warning(
            "polling_onlypublish_onchange is deprecated and will be removed in a future release. "
            "The component now always publishes only on change."
)
    await cg.register_component(var, config)
    # Get optional GEA3 UART component reference
    if CONF_GEA3_UART_ID in config:
        gea3_uart_component = await cg.get_variable(config[CONF_GEA3_UART_ID])
        cg.add(var.set_gea3_uart(gea3_uart_component))

    # Get optional GEA2 UART component reference
    if CONF_GEA2_UART_ID in config:
        gea2_uart_component = await cg.get_variable(config[CONF_GEA2_UART_ID])
        cg.add(var.set_gea2_uart(gea2_uart_component))

    # Set device ID if provided, otherwise it will be auto-generated
    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    # Set adapter address (defaults to 0xE4)
    cg.add(var.set_client_address(config[CONF_ADAPTER_ADDRESS]))

    # Set bridge mode configuration (config[CONF_MODE] is now an integer from cv.enum)
    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_polling_interval(config[CONF_POLLING_INTERVAL]))
    cg.add(var.set_appliance_api_parsing(config[CONF_APPLIANCE_API_PARSING]))
    cg.add(var.set_generate_device_config(config[CONF_GENERATE_DEVICE_CONFIG]))
    cg.add(var.set_throttle_rate_seconds(config[CONF_THROTTLE_RATE_SECONDS]))
    cg.add(var.set_filter_config_topics(config[CONF_FILTER_CONFIG_TOPICS]))
    # Create diagnostic sensors (auto-created by default, set to false to disable)
    await _create_diagnostic_sensor(config, CONF_ERD_PUBLISH_RATE_SENSOR, "ERD Publish Rate", "erd_publish_rate", "measurement", var, "set_erd_publish_rate_sensor")
    await _create_diagnostic_sensor(config, CONF_ERD_CACHE_ENTRIES_SENSOR, "ERD Cache Entries", "erd_cache_entries", "measurement", var, "set_erd_cache_entries_sensor", {"accuracy_decimals": 0})
    await _create_diagnostic_sensor(config, CONF_ERD_CACHE_UPDATES_SENSOR, "ERD Cache Update Rate", "erd_cache_updates", "measurement", var, "set_erd_cache_updates_sensor")
    await _create_diagnostic_sensor(config, CONF_MQTT_PUBLISH_RATE_SENSOR, "MQTT Publish Rate", "mqtt_publish_rate", "measurement", var, "set_mqtt_publish_rate_sensor")
    await _create_diagnostic_sensor(config, CONF_MQTT_DISCONNECT_COUNT_SENSOR, "MQTT Disconnect Count", "mqtt_disconnect_count", "total_increasing", var, "set_mqtt_disconnect_count_sensor", {"accuracy_decimals": 0})
    await _create_diagnostic_sensor(config, CONF_MQTT_DISCONNECT_DURATION_SENSOR, "MQTT Last Disconnect Duration", "mqtt_disconnect_duration", "measurement", var, "set_mqtt_disconnect_duration_sensor", {"unit_of_measurement": "ms"})


    # Create discovery refresh button (auto-created by default, set to false to disable)
    val = config.get(CONF_DISCOVERY_REFRESH_BUTTON, True)
    if val is not False:
        if val is True:
            val = {
                "name": "Discovery Refresh",
                CONF_ID: ID("discovery_refresh", is_declaration=True, type=DiscoveryRefreshButton),
                "disabled_by_default": False,
            }
        btn = cg.new_Pvariable(val[CONF_ID], var)
        await button.register_button(btn, val)

    # Register any user-configured custom ERDs
    for erd in config[CONF_CUSTOM_ERDS]:
        cg.add(var.add_custom_erd(erd))
    
