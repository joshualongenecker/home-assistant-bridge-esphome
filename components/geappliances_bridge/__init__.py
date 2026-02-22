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
CONF_MODE = "mode"
CONF_POLL_INTERVAL = "poll_interval"

# Mode options
MODE_SUBSCRIBE = "subscribe"
MODE_POLL = "poll"

# ERD series constants
ERD_SERIES_SIZE = 0x1000  # ERD series are grouped in 0x1000 increments
COMMON_ERD_SERIES = 0x0000  # Common ERDs for all appliances
ENERGY_ERD_SERIES = 0xD000  # Energy and diagnostic ERDs
ENERGY_ERD_MIN = 0xD000  # Start of energy ERD range
ENERGY_ERD_MAX = 0xDFFF  # End of energy ERD range

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


def extract_erds_from_section(section):
    """Extract ERD hex values from a section (common or featureApis)."""
    erds = set()
    
    if 'versions' in section:
        for version_data in section['versions'].values():
            # Extract from top-level required field
            if 'required' in version_data:
                for erd_item in version_data['required']:
                    if 'erd' in erd_item:
                        erds.add(erd_item['erd'])
            
            # Extract from features
            if 'features' in version_data:
                for feature in version_data['features']:
                    if 'required' in feature:
                        for erd_item in feature['required']:
                            if 'erd' in erd_item:
                                erds.add(erd_item['erd'])
                    if 'optional' in feature:
                        for erd_item in feature['optional']:
                            if 'erd' in erd_item:
                                erds.add(erd_item['erd'])
    
    return erds


def get_erd_series(erd_hex):
    """Get the series prefix for an ERD (e.g., '0x0000', '0x1000', etc.)."""
    try:
        erd_int = int(erd_hex, 16)
        # Round down to nearest ERD series boundary
        series = (erd_int // ERD_SERIES_SIZE) * ERD_SERIES_SIZE
        return f'0x{series:04X}'
    except (ValueError, TypeError):
        return None


def parse_appliance_api_for_erds():
    """Parse appliance_api.json to extract ERD lists for polling."""
    # Try to find the JSON file
    data = None
    json_filename = "appliance_api.json"
    
    search_paths = []
    seen_paths = set()
    
    component_dir = os.path.dirname(__file__)
    
    # Path 1: Local submodule
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
    
    # Try each path
    for location_name, json_path in search_paths:
        if os.path.exists(json_path):
            try:
                with open(json_path, 'r') as f:
                    data = json.load(f)
                _LOGGER.info("Loaded appliance API from %s: %s", location_name, json_path)
                break
            except Exception as e:
                _LOGGER.warning("Failed to load from %s (%s): %s", location_name, json_path, str(e))
    
    # If local paths failed, try fetching from GitHub as fallback
    if data is None:
        url = "https://raw.githubusercontent.com/geappliances/public-appliance-api-documentation/main/appliance_api.json"
        _LOGGER.warning("Could not find local library. Fetching from GitHub as fallback: %s", url)
        
        try:
            with urllib.request.urlopen(url, timeout=10) as response:
                data = json.loads(response.read().decode('utf-8'))
            _LOGGER.info("Successfully fetched appliance API from GitHub (fallback)")
        except urllib.error.HTTPError as e:
            _LOGGER.error(
                "HTTP error fetching appliance API documentation (status %d): %s. Using minimal fallback.", 
                e.code, str(e)
            )
            return {}, {}
        except urllib.error.URLError as e:
            _LOGGER.error(
                "Network error fetching appliance API documentation: %s. Using minimal fallback.", 
                str(e.reason)
            )
            return {}, {}
        except Exception as e:
            _LOGGER.error(
                "Unexpected error fetching appliance API documentation: %s. Using minimal fallback.", 
                str(e)
            )
            return {}, {}
    
    # Extract common ERDs
    common_erds = extract_erds_from_section(data.get('common', {}))
    _LOGGER.info("Found %d common ERDs", len(common_erds))
    
    # Extract appliance-specific ERDs by feature type
    appliance_erds = {}
    feature_apis = data.get('featureApis', {})
    for feature_type, feature_data in feature_apis.items():
        erds = extract_erds_from_section(feature_data)
        if erds:
            appliance_erds[feature_type] = erds
            _LOGGER.info("Found %d ERDs for appliance type %s (%s)", 
                        len(erds), feature_type, feature_data.get('name', 'Unknown'))
    
    return common_erds, appliance_erds


def categorize_erds_by_series(erds):
    """Categorize ERDs by their series (0x0000, 0x1000, etc.)."""
    series_dict = {}
    for erd_hex in erds:
        series = get_erd_series(erd_hex)
        if series:
            if series not in series_dict:
                series_dict[series] = []
            series_dict[series].append(erd_hex)
    
    # Sort ERDs within each series
    for series in series_dict:
        series_dict[series].sort(key=lambda x: int(x, 16))
    
    return series_dict


def generate_erd_lists_cpp():
    """Generate C++ code with ERD lists for polling.
    Returns a tuple of (declarations_code, definitions_code).
    Declarations go in the header via add_global().
    Definitions go in a source context.
    """
    common_erds, appliance_erds = parse_appliance_api_for_erds()
    
    if not common_erds:
        _LOGGER.warning("No ERDs found, using minimal fallback")
        common_erds = {'0x0001', '0x0002', '0x0008'}
    
    # Convert common ERDs to sorted list
    common_erd_list = sorted(list(common_erds), key=lambda x: int(x, 16))
    
    # Generate common ERDs array
    common_erds_str = ', '.join(common_erd_list)
    
    # Generate declarations for header file
    declarations = '''
// Auto-generated ERD declarations from public-appliance-api-documentation
#ifdef __cplusplus
extern "C" {
#endif
  extern const tiny_erd_t common_erds[];
  extern const size_t common_erd_count;
  extern const tiny_erd_t energy_erds[];
  extern const size_t energy_erd_count;
#ifdef __cplusplus
}
#endif
'''
    
    # Generate definitions for source file
    definitions = f'''
// Auto-generated ERD definitions from public-appliance-api-documentation
#ifdef __cplusplus
extern "C" {{
#endif
  // Common ERDs that apply to all appliances (0x0000 series)
  const tiny_erd_t common_erds[] = {{ {common_erds_str} }};
  const size_t common_erd_count = {len(common_erd_list)};
'''
    
    # Generate energy/diagnostic ERDs (0xD000-0xDFFF series)
    energy_erds = set()
    for erds in appliance_erds.values():
        for erd in erds:
            # Check if ERD is in energy/diagnostic range
            try:
                erd_value = int(erd, 16)
                if ENERGY_ERD_MIN <= erd_value <= ENERGY_ERD_MAX:
                    energy_erds.add(erd)
            except (ValueError, TypeError):
                continue
    
    if energy_erds:
        energy_erd_list = sorted(list(energy_erds), key=lambda x: int(x, 16))
        energy_erds_str = ', '.join(energy_erd_list)
        definitions += f'''
  // Energy and diagnostic ERDs (0xD000 series)
  const tiny_erd_t energy_erds[] = {{ {energy_erds_str} }};
  const size_t energy_erd_count = {len(energy_erd_list)};
'''
    else:
        definitions += '''
  // No energy ERDs found
  const tiny_erd_t energy_erds[] = {};
  const size_t energy_erd_count = 0;
'''
    
    definitions += '''#ifdef __cplusplus
}
#endif
'''
    
    # Generate appliance-specific ERD arrays by series (in anonymous namespace)
    definitions += '\nnamespace {\n  // Appliance-specific ERD lists by feature type\n'
    
    for feature_type, erds in sorted(appliance_erds.items()):
        # Group ERDs by series for this appliance type
        series_dict = categorize_erds_by_series(erds)
        
        # Generate arrays for each series (excluding common and energy ERDs)
        for series, erd_list in sorted(series_dict.items()):
            # Skip common and energy series (compare as integers for correctness)
            try:
                series_int = int(series, 16)
                if series_int == COMMON_ERD_SERIES or series_int == ENERGY_ERD_SERIES:
                    continue
            except (ValueError, TypeError):
                continue
            
            erd_str = ', '.join(erd_list)
            safe_feature_name = feature_type.replace('-', '_')
            definitions += f'  constexpr tiny_erd_t appliance_type_{safe_feature_name}_series_{series}_erds[] = {{ {erd_str} }};\n'
            definitions += f'  constexpr size_t appliance_type_{safe_feature_name}_series_{series}_erd_count = {len(erd_list)};\n\n'
    
    definitions += '}\n'
    
    return declarations, definitions

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_DEVICE_ID): cv.string,
        cv.Optional(CONF_MODE, default=MODE_SUBSCRIBE): cv.enum(
            {MODE_SUBSCRIBE: MODE_SUBSCRIBE, MODE_POLL: MODE_POLL}, upper=False
        ),
        cv.Optional(CONF_POLL_INTERVAL, default=10000): cv.int_range(min=1000, max=300000),
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

    # Set device ID if provided, otherwise it will be auto-generated
    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    
    # Set mode (subscribe or poll)
    mode = config[CONF_MODE]
    if mode == MODE_POLL:
        cg.add(var.set_mode_poll())
        # Set polling interval
        poll_interval = config[CONF_POLL_INTERVAL]
        cg.add(var.set_poll_interval(poll_interval))
        _LOGGER.info("Configuring bridge in POLL mode with %d ms interval", poll_interval)
    else:
        cg.add(var.set_mode_subscribe())
        _LOGGER.info("Configuring bridge in SUBSCRIBE mode (default)")
    
    # Load appliance types from JSON and generate C++ mapping function
    appliance_types = load_appliance_types()
    function_code = generate_appliance_type_function(appliance_types)
    
    # Add the generated function to the global namespace
    cg.add_global(cg.RawStatement(function_code))
    
    # Generate ERD lists for polling mode
    if mode == MODE_POLL:
        declarations, definitions = generate_erd_lists_cpp()
        
        # Add declarations to the header (global scope)
        cg.add_global(cg.RawStatement(declarations))
        
        # Write definitions to a source file in the component directory
        # This file will be compiled by ESPHome
        import os
        component_dir = os.path.dirname(os.path.abspath(__file__))
        erd_defs_path = os.path.join(component_dir, "geappliances_erd_definitions.cpp")
        
        with open(erd_defs_path, 'w') as f:
            f.write('#include "tiny_gea_constants.h"\n\n')
            f.write(definitions)
        
        _LOGGER.info("Generated ERD definitions file: %s", erd_defs_path)
