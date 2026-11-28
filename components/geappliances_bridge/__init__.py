"""ESPHome component for GE Appliances Bridge."""
from __future__ import annotations

import logging
import re
import zlib
from typing import Any
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, sensor, uart
from esphome.const import CONF_ID
from esphome.core import CORE

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@joshualongenecker"]
DEPENDENCIES = ["uart", "mqtt"]
AUTO_LOAD = ["sensor", "button"]

CONF_GEA3_UART_ID = "gea3_uart_id"
CONF_GEA2_UART_ID = "gea2_uart_id"

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
CONF_RECONNECT_APPLIANCE_BUTTON = "reconnect_appliance_button"
CONF_CUSTOM_HA_DISCOVERY_FILE = "custom_ha_discovery_file"
CONF_BOARD_ADDRESS = "board_address"

MODE_POLL = "poll"
MODE_SUBSCRIBE = "subscribe"
MODE_AUTO = "auto"

MODE_POLL_VALUE = 0
MODE_SUBSCRIBE_VALUE = 1
MODE_AUTO_VALUE = 2

geappliances_bridge_ns = cg.esphome_ns.namespace("geappliances_bridge")
GeappliancesBridge = geappliances_bridge_ns.class_(
    "GeappliancesBridge", cg.Component
)

DiscoveryRefreshButton = geappliances_bridge_ns.class_(
    "DiscoveryRefreshButton", button.Button
)

ReconnectApplianceButton = geappliances_bridge_ns.class_(
    "ReconnectApplianceButton", button.Button
)

DIAGNOSTIC_ENTITY_KEYS = (
    CONF_ERD_PUBLISH_RATE_SENSOR,
    CONF_ERD_CACHE_ENTRIES_SENSOR,
    CONF_ERD_CACHE_UPDATES_SENSOR,
    CONF_MQTT_PUBLISH_RATE_SENSOR,
    CONF_MQTT_DISCONNECT_COUNT_SENSOR,
    CONF_MQTT_DISCONNECT_DURATION_SENSOR,
    CONF_DISCOVERY_REFRESH_BUTTON,
    CONF_RECONNECT_APPLIANCE_BUTTON,
)

def expand_diagnostic_entity_defaults(config: dict[str, Any]) -> dict[str, Any]:
    """Normalize a literal `true` diagnostic-entity option to `{}` before validation.

    ESPHome validates schema defaults, so an absent option validated as a mapping receives
    the full entity defaults (name, state class, id, mqtt_id). A literal `true` matched the
    boolean branch instead and produced an entity with no MQTT wrapper.
    """
    for key in DIAGNOSTIC_ENTITY_KEYS:
        if config.get(key) is True:
            config[key] = {}
    return config

async def _create_diagnostic_sensor(config: dict[str, Any], config_key: str, var: Any, setter_name: str) -> None:
    """Create a diagnostic sensor unless its option is `false`.

    The option is validated by sensor.sensor_schema(), so the value already carries the
    name, state class, id and the mqtt_id that codegen needs to publish the entity.
    """
    val = config.get(config_key)
    if val is None or val is False:
        return
    sens = await sensor.new_sensor(val)
    cg.add(getattr(var, setter_name)(sens))

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

def validate_board_address_not_adapter_address(config: dict[str, Any]) -> dict[str, Any]:
    """Validate that board_address does not collide with adapter_address."""
    if CONF_BOARD_ADDRESS in config:
        board_addr = config[CONF_BOARD_ADDRESS]
        adapter_addr = config.get(CONF_ADAPTER_ADDRESS, 0xE4)
        if board_addr == adapter_addr:
            raise cv.Invalid(
                f"board_address (0x{board_addr:02X}) must not match "
                f"adapter_address (0x{adapter_addr:02X}) — the bridge would probe itself"
            )
    return config

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.Optional(CONF_GEA3_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_GEA2_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_ADAPTER_ADDRESS, default=0xE4): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_BOARD_ADDRESS): cv.int_range(min=0x00, max=0xFF),
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
        cv.Optional(CONF_ERD_PUBLISH_RATE_SENSOR, default={}): cv.Any(
            cv.boolean_false,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="ERD Publish Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_ERD_CACHE_ENTRIES_SENSOR, default={}): cv.Any(
            cv.boolean_false,
            sensor.sensor_schema(state_class="measurement", accuracy_decimals=0).extend(cv.Schema({
                cv.Optional("name", default="ERD Cache Entries"): cv.string,
            })),
        ),
        cv.Optional(CONF_ERD_CACHE_UPDATES_SENSOR, default={}): cv.Any(
            cv.boolean_false,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="ERD Cache Update Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_PUBLISH_RATE_SENSOR, default={}): cv.Any(
            cv.boolean_false,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="MQTT Publish Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_DISCONNECT_COUNT_SENSOR, default={}): cv.Any(
            cv.boolean_false,
            sensor.sensor_schema(state_class="total_increasing", accuracy_decimals=0).extend(cv.Schema({
                cv.Optional("name", default="MQTT Disconnect Count"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_DISCONNECT_DURATION_SENSOR, default={}): cv.Any(
            cv.boolean_false,
            sensor.sensor_schema(state_class="measurement", unit_of_measurement="ms").extend(cv.Schema({
                cv.Optional("name", default="MQTT Last Disconnect Duration"): cv.string,
            })),
        ),
        cv.Optional(CONF_FILTER_CONFIG_TOPICS, default=True): cv.boolean,
        cv.Optional(CONF_DISCOVERY_REFRESH_BUTTON, default={}): cv.Any(
            cv.boolean_false,
            button.button_schema(DiscoveryRefreshButton).extend(cv.Schema({
                cv.Optional("name", default="Discovery Refresh"): cv.string,
            })),
        ),
        cv.Optional(CONF_RECONNECT_APPLIANCE_BUTTON, default={}): cv.Any(
            cv.boolean_false,
            button.button_schema(ReconnectApplianceButton, icon="mdi:lan-connect").extend(cv.Schema({
                cv.Optional("name", default="Reconnect Appliance"): cv.string,
            })),
        ),
        cv.Optional(CONF_CUSTOM_HA_DISCOVERY_FILE): cv.file_,
    }
).extend(cv.COMPONENT_SCHEMA)
CONFIG_SCHEMA = cv.All(expand_diagnostic_entity_defaults, CONFIG_SCHEMA, validate_at_least_one_uart, validate_board_address_not_adapter_address)

async def to_code(config: dict[str, Any]) -> None:
    """Generate C++ code for the component.

    Args:
        config: Configuration dictionary
    """
    cg.add_library("tiny-gea-api", None, "https://github.com/geappliances/tiny-gea-api#4fa8fee8297e24baa91bfe4a464088a73e7c6a5a")
    if CORE.is_esp32:
        cg.add_define("USE_MQTT_IDF_ENQUEUE")
    
    var = cg.new_Pvariable(config[CONF_ID])
    if CONF_POLLING_ONLY_PUBLISH_ON_CHANGE in config:
        _LOGGER.warning(
            "polling_onlypublish_onchange is deprecated and will be removed in a future release. "
            "The component now always publishes only on change."
)
    await cg.register_component(var, config)
    if CONF_GEA3_UART_ID in config:
        cg.add_define("GEA_BRIDGE_HAS_GEA3")
    if CONF_GEA2_UART_ID in config:
        cg.add_define("GEA_BRIDGE_HAS_GEA2")

    if CONF_GEA3_UART_ID in config:
        gea3_uart_component = await cg.get_variable(config[CONF_GEA3_UART_ID])
        cg.add(var.set_gea3_uart(gea3_uart_component))

    if CONF_GEA2_UART_ID in config:
        gea2_uart_component = await cg.get_variable(config[CONF_GEA2_UART_ID])
        cg.add(var.set_gea2_uart(gea2_uart_component))

    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    cg.add(var.set_client_address(config[CONF_ADAPTER_ADDRESS]))

    if CONF_BOARD_ADDRESS in config:
        cg.add(var.set_board_address(config[CONF_BOARD_ADDRESS]))

    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_polling_interval(config[CONF_POLLING_INTERVAL]))
    cg.add(var.set_appliance_api_parsing(config[CONF_APPLIANCE_API_PARSING]))
    cg.add(var.set_generate_device_config(config[CONF_GENERATE_DEVICE_CONFIG]))
    cg.add(var.set_throttle_rate_seconds(config[CONF_THROTTLE_RATE_SECONDS]))
    cg.add(var.set_filter_config_topics(config[CONF_FILTER_CONFIG_TOPICS]))
    if CONF_CUSTOM_HA_DISCOVERY_FILE in config:
        custom_file = CORE.relative_config_path(config[CONF_CUSTOM_HA_DISCOVERY_FILE])
        contents = Path(custom_file).read_text(encoding="utf-8")
        required = ("custom_ha_discovery_data", "custom_ha_discovery_chunks",
                    "CUSTOM_HA_DISCOVERY_CHUNK_COUNT", "CUSTOM_HA_DISCOVERY_MAX_CHUNK",
                    "custom_ha_discovery_erds", "CUSTOM_HA_DISCOVERY_ERD_COUNT")
        if not all(token in contents for token in required):
            raise cv.Invalid(
                "custom_ha_discovery_file must be a generated header containing "
                "custom_ha_discovery_data, custom_ha_discovery_chunks, "
                "CUSTOM_HA_DISCOVERY_CHUNK_COUNT, CUSTOM_HA_DISCOVERY_MAX_CHUNK, "
                "custom_ha_discovery_erds, and CUSTOM_HA_DISCOVERY_ERD_COUNT. "
                "See the project documentation for the custom profile generator.")
        cg.add_global(cg.RawStatement(contents))
        if "CUSTOM_HA_DISCOVERY_DATA_HASH" not in contents:
            legacy_hash = zlib.crc32(contents.encode("utf-8")) & 0xFFFFFFFF
            cg.add_global(cg.RawStatement(
                f"#define CUSTOM_HA_DISCOVERY_DATA_HASH 0x{legacy_hash:08x}u"))
        cg.add(var.set_custom_ha_discovery_data(
            cg.RawExpression("custom_ha_discovery_data"),
            cg.RawExpression("custom_ha_discovery_chunks"),
            cg.RawExpression("CUSTOM_HA_DISCOVERY_CHUNK_COUNT"),
            cg.RawExpression("CUSTOM_HA_DISCOVERY_MAX_CHUNK"),
            cg.RawExpression("CUSTOM_HA_DISCOVERY_DATA_HASH")))
        match = re.search(r"#define\\s+CUSTOM_HA_DISCOVERY_ERD_COUNT\\s+(\\d+)", contents)
        generated_erd_count = int(match.group(1)) if match else 0
        if generated_erd_count + len(config[CONF_CUSTOM_ERDS]) > 128:
            raise cv.Invalid(
                f"custom_ha_discovery_file contains {generated_erd_count} ERDs; together with custom_erds "
                "this exceeds the 128 ERD polling limit")
        if "custom_ha_discovery_erd_addresses" in contents:
            cg.add(var.add_custom_erds_with_addresses(
                cg.RawExpression("custom_ha_discovery_erds"),
                cg.RawExpression("custom_ha_discovery_erd_addresses"),
                cg.RawExpression("CUSTOM_HA_DISCOVERY_ERD_COUNT")))
        else:
            cg.add(var.add_custom_erds(
                cg.RawExpression("custom_ha_discovery_erds"),
                cg.RawExpression("CUSTOM_HA_DISCOVERY_ERD_COUNT")))
    await _create_diagnostic_sensor(config, CONF_ERD_PUBLISH_RATE_SENSOR, var, "set_erd_publish_rate_sensor")
    await _create_diagnostic_sensor(config, CONF_ERD_CACHE_ENTRIES_SENSOR, var, "set_erd_cache_entries_sensor")
    await _create_diagnostic_sensor(config, CONF_ERD_CACHE_UPDATES_SENSOR, var, "set_erd_cache_updates_sensor")
    await _create_diagnostic_sensor(config, CONF_MQTT_PUBLISH_RATE_SENSOR, var, "set_mqtt_publish_rate_sensor")
    await _create_diagnostic_sensor(config, CONF_MQTT_DISCONNECT_COUNT_SENSOR, var, "set_mqtt_disconnect_count_sensor")
    await _create_diagnostic_sensor(config, CONF_MQTT_DISCONNECT_DURATION_SENSOR, var, "set_mqtt_disconnect_duration_sensor")

    val = config.get(CONF_DISCOVERY_REFRESH_BUTTON)
    if val is not None and val is not False:
        btn = cg.new_Pvariable(val[CONF_ID], var)
        await button.register_button(btn, val)

    val = config.get(CONF_RECONNECT_APPLIANCE_BUTTON)
    if val is not None and val is not False:
        btn = cg.new_Pvariable(val[CONF_ID], var)
        await button.register_button(btn, val)

    for erd in config[CONF_CUSTOM_ERDS]:
        cg.add(var.add_custom_erd(erd))
