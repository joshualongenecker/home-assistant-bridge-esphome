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
    """Load appliance type mappings from the submodule JSON file."""
    # Get the path to the submodule JSON file
    component_dir = os.path.dirname(__file__)
    json_path = os.path.join(
        component_dir, 
        "..", 
        "..", 
        "public-appliance-api-documentation", 
        "appliance_api_erd_definitions.json"
    )
    json_path = os.path.normpath(json_path)
    
    if not os.path.exists(json_path):
        _LOGGER.warning(
            "Appliance API documentation JSON not found at %s. Using fallback mapping.", 
            json_path
        )
        return {
            0: "Unknown",
            255: "Unknown"
        }
    
    try:
        with open(json_path, 'r') as f:
            data = json.load(f)
        
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
                    
                    _LOGGER.info("Loaded %d appliance type mappings from JSON", len(mapping))
                    return mapping
    except Exception as e:
        _LOGGER.error(
            "Failed to parse appliance API documentation JSON: %s. Using fallback mapping.", 
            str(e)
        )
    
    # Fallback mapping
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
