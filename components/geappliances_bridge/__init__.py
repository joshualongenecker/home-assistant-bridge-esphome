"""ESPHome component for GE Appliances Bridge."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart, mqtt
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
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
CONF_CLIENT_ADDRESS = "client_address"

geappliances_bridge_ns = cg.esphome_ns.namespace("geappliances_bridge")
GeappliancesBridge = geappliances_bridge_ns.class_(
    "GeappliancesBridge", cg.Component
)


def sanitize_appliance_name(name):
    """Sanitize appliance type name for use in C++ strings."""
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
    """Load appliance type mappings from the API documentation."""
    # Try to load from submodule first (for local development)
    component_dir = os.path.dirname(__file__)
    json_path = os.path.join(
        component_dir, 
        "..", 
        "..", 
        "public-appliance-api-documentation", 
        "appliance_api_erd_definitions.json"
    )
    json_path = os.path.normpath(json_path)
    
    data = None
    
    # Try loading from local submodule first
    if os.path.exists(json_path):
        try:
            with open(json_path, 'r') as f:
                data = json.load(f)
            _LOGGER.info("Loaded appliance types from local submodule")
        except Exception as e:
            _LOGGER.warning("Failed to load from local submodule: %s", str(e))
    
    # If local load failed, fetch from GitHub
    if data is None:
        url = "https://raw.githubusercontent.com/geappliances/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
        _LOGGER.info("Fetching appliance types from GitHub: %s", url)
        
        try:
            with urllib.request.urlopen(url, timeout=10) as response:
                data = json.loads(response.read().decode('utf-8'))
            _LOGGER.info("Successfully fetched appliance types from GitHub")
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

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_DEVICE_ID): cv.string,
        cv.Optional(CONF_CLIENT_ADDRESS, default=0xE4): cv.hex_uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate C++ code for the component."""
    # Add library dependencies
    cg.add_library("https://github.com/ryanplusplus/tiny", None)
    cg.add_library("https://github.com/geappliances/tiny-gea-api#develop", None)
    
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get UART component reference
    uart_component = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(uart_component))

    # Set device ID if provided, otherwise it will be auto-generated
    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    # Set client address
    cg.add(var.set_client_address(config[CONF_CLIENT_ADDRESS]))
    
    # Load appliance types from JSON and generate C++ mapping function
    appliance_types = load_appliance_types()
    function_code = generate_appliance_type_function(appliance_types)
    
    # Add the generated function to the global namespace
    cg.add_global(cg.RawStatement(function_code))
