"""ESPHome component for GE Appliances Bridge."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart, mqtt
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
)
from pathlib import Path

CODEOWNERS = ["@joshualongenecker"]
DEPENDENCIES = ["uart", "mqtt"]
AUTO_LOAD = []

CONF_DEVICE_ID = "device_id"
CONF_CLIENT_ADDRESS = "client_address"

geappliances_bridge_ns = cg.esphome_ns.namespace("geappliances_bridge")
GeappliancesBridge = geappliances_bridge_ns.class_(
    "GeappliancesBridge", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Required(CONF_DEVICE_ID): cv.string,
        cv.Optional(CONF_CLIENT_ADDRESS, default=0xE4): cv.hex_uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate C++ code for the component."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get UART component reference
    uart_component = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(uart_component))

    # Set device ID
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    # Set client address
    cg.add(var.set_client_address(config[CONF_CLIENT_ADDRESS]))
    
    # Add include paths for dependencies
    # ESPHome will look for these relative to the component directory
    cg.add_build_flag("-I" + str(Path(__file__).parent / ".." / ".." / "lib" / "tiny" / "include"))
    cg.add_build_flag("-I" + str(Path(__file__).parent / ".." / ".." / "lib" / "tiny-gea-api" / "include"))
    cg.add_build_flag("-I" + str(Path(__file__).parent / ".." / ".." / "include"))
