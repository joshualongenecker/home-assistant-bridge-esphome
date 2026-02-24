"""ESPHome component for GE Appliances Bridge."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart, mqtt
from esphome.core import CORE
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    CONF_BAUD_RATE,
    CONF_TX_PIN,
    CONF_RX_PIN,
)
import json
import os
import re
import logging
import urllib.request
import urllib.error

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@joshualongenecker"]
DEPENDENCIES = ["uart", "mqtt"]
AUTO_LOAD = []

CONF_DEVICE_ID = "device_id"
CONF_MODE = "mode"
CONF_POLLING_INTERVAL = "polling_interval"

# Mode options
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


def sanitize_appliance_name(name):
    """Sanitize appliance type name for use in C++ identifiers.
    
    Replaces special characters with readable equivalents and removes
    any non-alphanumeric characters to create valid C++ identifiers.
    
    Args:
        name: The appliance type name to sanitize
        
    Returns:
        A sanitized string suitable for use as a C++ identifier
    """
    # Replace special characters with more readable equivalents
    replacements = {
        ' ': '',
        '/': '',
        '&': 'And',
        '-': '',
        '(': '',
        ')': '',
    }
    
    result = name
    for old, new in replacements.items():
        result = result.replace(old, new)
    
    # Remove any remaining non-alphanumeric characters
    result = re.sub(r'[^a-zA-Z0-9]', '', result)
    return result


def load_appliance_types():
    """Load appliance type mappings from the API documentation library.
    
    Tries multiple locations to find the appliance type definitions JSON:
    1. Local submodule directory (for development with checked out repo)
    2. ESPHome library cache in user's home directory (~/.esphome/external_files/libraries/)
    3. ESPHome library cache in /config directory (Home Assistant add-on)
    4. ESPHome library cache relative to component (build directory)
    5. Parent directories (alternative library location)
    6. GitHub as fallback (when no local copy is available)
    
    Returns:
        Dictionary mapping appliance type IDs (int) to names (str)
    """
    # ESPHome downloads libraries to .esphome/external_files/libraries
    # We need to check multiple possible locations
    
    data = None
    json_filename = "appliance_api_erd_definitions.json"
    
    # Try to find the JSON file in common locations
    search_paths = []
    seen_paths = set()  # Track paths to avoid duplicates
    
    # Path 1: Local submodule (for local development)
    component_dir = os.path.dirname(__file__)
    local_submodule_path = os.path.normpath(os.path.join(
        component_dir, "..", "..", "lib", "public-appliance-api-documentation", json_filename
    ))
    search_paths.append(("local submodule", local_submodule_path))
    seen_paths.add(local_submodule_path)
    
    # Path 2: ESPHome library cache in user's home directory
    home_dir = os.path.expanduser("~")
    esphome_cache_path = os.path.join(
        home_dir, ".esphome", "external_files", "libraries",
        "public-appliance-api-documentation", json_filename
    )
    if esphome_cache_path not in seen_paths:
        search_paths.append(("ESPHome cache (home)", esphome_cache_path))
        seen_paths.add(esphome_cache_path)
    
    # Path 3: ESPHome library cache in /config (Home Assistant add-on)
    config_esphome_cache_path = os.path.join(
        "/config", ".esphome", "external_files", "libraries",
        "public-appliance-api-documentation", json_filename
    )
    if config_esphome_cache_path not in seen_paths:
        search_paths.append(("ESPHome cache (/config)", config_esphome_cache_path))
        seen_paths.add(config_esphome_cache_path)
    
    # Path 4: ESPHome library cache relative to component
    # Sometimes ESPHome puts libraries relative to the build directory
    build_cache_path = os.path.normpath(os.path.join(
        component_dir, "..", "..", ".esphome", "external_files", "libraries",
        "public-appliance-api-documentation", json_filename
    ))
    if build_cache_path not in seen_paths:
        search_paths.append(("ESPHome cache (relative)", build_cache_path))
        seen_paths.add(build_cache_path)
    
    # Path 5: Check parent directories for the library
    parent_dir = os.path.dirname(os.path.dirname(component_dir))
    alt_library_path = os.path.normpath(os.path.join(
        parent_dir, "lib", "public-appliance-api-documentation", json_filename
    ))
    if alt_library_path not in seen_paths:
        search_paths.append(("parent library path", alt_library_path))
        seen_paths.add(alt_library_path)
    
    # Try each path
    for location_name, json_path in search_paths:
        if os.path.exists(json_path):
            try:
                with open(json_path, 'r') as f:
                    data = json.load(f)
                _LOGGER.info("Loaded appliance types from %s: %s", location_name, json_path)
                break
            except Exception as e:
                _LOGGER.warning("Failed to load from %s (%s): %s", location_name, json_path, str(e))
    
    # If local paths failed, try fetching from GitHub as fallback
    if data is None:
        url = "https://raw.githubusercontent.com/geappliances/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
        _LOGGER.warning("Could not find local library. Fetching from GitHub as fallback: %s", url)
        
        try:
            with urllib.request.urlopen(url, timeout=10) as response:
                data = json.loads(response.read().decode('utf-8'))
            _LOGGER.info("Successfully fetched appliance types from GitHub (fallback)")
        except urllib.error.HTTPError as e:
            _LOGGER.error(
                "HTTP error fetching appliance API documentation (status %d): %s. Using fallback mapping.", 
                e.code, str(e)
            )
            return {
                0: "Unknown",
                255: "Unknown"
            }
        except urllib.error.URLError as e:
            _LOGGER.error(
                "Network error fetching appliance API documentation: %s. Using fallback mapping.", 
                str(e.reason)
            )
            return {
                0: "Unknown",
                255: "Unknown"
            }
        except Exception as e:
            _LOGGER.error(
                "Unexpected error fetching appliance API documentation: %s. Using fallback mapping.", 
                str(e)
            )
            return {
                0: "Unknown",
                255: "Unknown"
            }
    
    # Parse the data
    try:
        # Find the ERD with id "0x0008" (Appliance Type)
        for erd in data.get("erds", []):
            if erd.get("id") == "0x0008":
                # Extract the enum values
                erd_data = erd.get("data", [])
                if erd_data and erd_data[0].get("type") == "enum":
                    values = erd_data[0].get("values", {})
                    # Convert string keys to integers and sanitize values for C++
                    mapping = {}
                    for key, value in values.items():
                        int_key = int(key)
                        sanitized = sanitize_appliance_name(value)
                        mapping[int_key] = sanitized
                    
                    _LOGGER.info("Loaded %d appliance type mappings", len(mapping))
                    return mapping
    except Exception as e:
        _LOGGER.error("Failed to parse appliance types: %s", str(e))
    
    # Fallback mapping
    _LOGGER.warning("Using fallback appliance type mapping")
    return {
        0: "Unknown",
        255: "Unknown"
    }


def generate_appliance_type_function(appliance_types):
    """Generate C++ code for the appliance type to string function."""
    # Generate switch cases with consistent indentation
    cases = []
    for type_id, type_name in sorted(appliance_types.items()):
        cases.append(f'    case {type_id}: return "{type_name}";')
    
    cases_str = "\n".join(cases)
    
    # Generate the function with consistent 2-space indentation
    function_code = f'''
std::string appliance_type_to_string(uint8_t appliance_type) {{
  // Auto-generated from public-appliance-api-documentation
  // ERD 0x0008 - Appliance Type enum mapping
  switch (appliance_type) {{
{cases_str}
    default: return "Unknown";
  }}
}}
'''
    return function_code


def format_pin_for_display(pin):
    """Format a pin configuration for human-readable display.
    
    Args:
        pin: Pin configuration which can be a string, int, or pin object
        
    Returns:
        Formatted pin string (e.g., "GPIO21")
    """
    # Pin might be a string (e.g., "GPIO21"), int (e.g., 21), or a pin object
    if hasattr(pin, 'number'):
        # Pin object with number attribute
        return f"GPIO{pin.number}"
    elif isinstance(pin, int):
        # Integer pin number
        return f"GPIO{pin}"
    elif isinstance(pin, str):
        # String representation (might already include GPIO prefix)
        return str(pin)
    else:
        # Fallback to string representation
        return str(pin)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_DEVICE_ID): cv.string,
        cv.Optional(CONF_MODE, default=MODE_AUTO): cv.enum(
            {
                MODE_POLL: MODE_POLL_VALUE,
                MODE_SUBSCRIBE: MODE_SUBSCRIBE_VALUE,
                MODE_AUTO: MODE_AUTO_VALUE,
            },
            upper=False
        ),
        cv.Optional(CONF_POLLING_INTERVAL, default=10000): cv.positive_int,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate C++ code for the component."""
    # Add library dependencies
    cg.add_library("https://github.com/ryanplusplus/tiny", None)
    cg.add_library("https://github.com/geappliances/tiny-gea-api#develop", None)
    # Add public-appliance-api-documentation as a library dependency
    # This allows users to control the version by updating the library reference
    cg.add_library("https://github.com/geappliances/public-appliance-api-documentation", None)
    
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get UART component reference
    uart_component = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(uart_component))

    # Get UART configuration from global config to pass pin information
    # This allows the component to display the actual UART configuration in logs
    uart_id = config[CONF_UART_ID]
    if CORE.config and "uart" in CORE.config:
        uart_configs = CORE.config["uart"]
        # uart_configs might be a single dict or a list of dicts
        if not isinstance(uart_configs, list):
            uart_configs = [uart_configs]
        
        # Find the UART config that matches our ID
        for uart_config in uart_configs:
            if uart_config.get(CONF_ID) == uart_id:
                # Extract pin information and baud rate
                if CONF_TX_PIN in uart_config:
                    tx_pin_str = format_pin_for_display(uart_config[CONF_TX_PIN])
                    cg.add(var.set_uart_tx_pin(tx_pin_str))
                
                if CONF_RX_PIN in uart_config:
                    rx_pin_str = format_pin_for_display(uart_config[CONF_RX_PIN])
                    cg.add(var.set_uart_rx_pin(rx_pin_str))
                
                if CONF_BAUD_RATE in uart_config:
                    baud_rate = uart_config[CONF_BAUD_RATE]
                    cg.add(var.set_uart_baud_rate(baud_rate))
                
                break

    # Set device ID if provided, otherwise it will be auto-generated
    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    
    # Set mode configuration (config[CONF_MODE] is now an integer from cv.enum)
    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_polling_interval(config[CONF_POLLING_INTERVAL]))
    
    # Load appliance types from JSON and generate C++ mapping function
    appliance_types = load_appliance_types()
    function_code = generate_appliance_type_function(appliance_types)
    
    # Add the generated function to the global namespace
    cg.add_global(cg.RawStatement(function_code))
