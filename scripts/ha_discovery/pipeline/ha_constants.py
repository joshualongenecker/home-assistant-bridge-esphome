#!/usr/bin/env python3
"""
Shared Home Assistant constants for all scripts.
Single source of truth for device class keywords, unit mappings, and type definitions.
"""

from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
ERD_DEFINITIONS_FILE = SCRIPT_DIR.parent.parent.parent / "appliance_api_erd_definitions.json"

NUMERIC_TYPES = {"u8", "u16", "u32", "i8", "i16", "i32"}

DEVICE_CLASS_KEYWORDS = {
    "temperature": ["temperature", "temp"],
    "voltage": ["voltage", "volts"],
    "power": ["power"],
    "humidity": ["humidity"],
    "pressure": ["pressure"],
    "energy": ["energy", "watt seconds", "watt-hours", "wh"],
    "duration": ["duration", "timer", "timeout"],
    "frequency": ["frequency", "hertz"],
    "weight": ["weight", "mass"],
    "distance": ["distance"],
    "battery": ["battery"],
    "signal_strength": ["rssi"],
    "illuminance": ["illuminance", "lux"],
}

DEVICE_CLASS_EXCLUSIONS = {
    "power": ["power off", "power on", "power level"],
    "current": ["current limit", "current stage", "current setting", "current status", "current parameters"],
    "duration": ["dry time", "sha", "time of use", "padding", "enabled", "update",
                 "remaining", "elapsed", "runtime", "cook time", "cycle time",
                 "delay time", "time remaining", "time after", "startup time",
                 "uptime", "run time", "warming time"],
}

UNIT_KEYWORD_MAP = {
    "volts": "V",
    "amps": "A",
    "watts": "W",
    "fahrenheit": "°F",
    "degf": "°F",
    "celsius": "°C",
    "degc": "°C",
    "percent": "%",
    "hertz": "Hz",
    "minutes": "min",
    "seconds": "s",
    "gallons": "gal",
    "liters": "L",
    "hours": "h",
}

BINARY_SENSOR_KEYWORDS = {"on", "off", "true", "false", "enabled", "disabled"}

TOTAL_STATE_CLASSES = {"energy", "gas", "water", "volume"}

MEASUREMENT_DEVICE_CLASSES = {
    "temperature", "voltage", "current", "power", "humidity",
    "pressure", "frequency", "weight",
    "distance", "battery", "signal_strength", "illuminance",
}

VALID_STATE_CLASSES = {"measurement", "total", "total_increasing", "measurement_angle"}

UNIT_IMPLYING_DEVICE_CLASSES = {
    "absolute_humidity", "apparent_power", "area", "atmospheric_pressure",
    "battery", "blood_glucose_concentration", "carbon_monoxide",
    "carbon_dioxide", "conductivity", "current", "data_rate", "data_size",
    "distance", "duration", "energy", "energy_distance", "energy_storage",
    "frequency", "gas", "humidity", "illuminance", "irradiance", "moisture",
    "monetary", "nitrogen_dioxide", "nitrogen_monoxide", "nitrous_oxide",
    "ozone", "pm1", "pm10", "pm25", "pm4", "power", "power_factor",
    "precipitation", "precipitation_intensity", "pressure", "reactive_energy",
    "reactive_power", "signal_strength", "sound_pressure",
    "sulphur_dioxide", "temperature", "temperature_delta",
    "volatile_organic_compounds", "volatile_organic_compounds_parts",
    "voltage", "volume_storage", "volume_flow_rate",
    "water", "weight", "wind_direction", "wind_speed",
}

VALID_DEVICE_CLASSES = {
    "sensor": {
        "absolute_humidity", "apparent_power", "aqi", "area",
        "atmospheric_pressure", "battery", "blood_glucose_concentration",
        "carbon_monoxide", "carbon_dioxide", "conductivity", "current",
        "data_rate", "data_size", "date", "distance", "duration",
        "energy", "energy_distance", "energy_storage", "enum",
        "frequency", "gas", "humidity", "illuminance", "irradiance",
        "moisture", "monetary", "nitrogen_dioxide", "nitrogen_monoxide",
        "nitrous_oxide", "ozone", "ph", "pm1", "pm10", "pm25", "pm4",
        "power", "power_factor", "precipitation", "precipitation_intensity",
        "pressure", "reactive_energy", "reactive_power", "signal_strength",
        "sound_pressure", "speed", "sulphur_dioxide", "temperature",
        "temperature_delta", "timestamp", "uptime",
        "volatile_organic_compounds", "volatile_organic_compounds_parts",
        "voltage", "volume", "volume_storage", "volume_flow_rate",
        "water", "weight", "wind_direction", "wind_speed",
    },
    "binary_sensor": {
        "battery", "battery_charging", "carbon_monoxide", "cold",
        "connectivity", "door", "garage_door", "gas", "heat",
        "light", "lock", "moisture", "motion", "moving",
        "occupancy", "opening", "plug", "power", "presence",
        "problem", "running", "safety", "smoke", "sound",
        "tamper", "update", "vibration", "window",
    },
    "number": {
        "absolute_humidity", "apparent_power", "aqi", "area",
        "atmospheric_pressure", "battery", "blood_glucose_concentration",
        "carbon_monoxide", "carbon_dioxide", "conductivity", "current",
        "data_rate", "data_size", "distance", "duration",
        "energy", "energy_distance", "energy_storage", "frequency",
        "gas", "humidity", "illuminance", "irradiance", "moisture",
        "monetary", "nitrogen_dioxide", "nitrogen_monoxide",
        "nitrous_oxide", "ozone", "ph", "pm1", "pm10", "pm25", "pm4",
        "power", "power_factor", "precipitation", "precipitation_intensity",
        "pressure", "reactive_energy", "reactive_power", "signal_strength",
        "sound_pressure", "speed", "sulphur_dioxide", "temperature",
        "temperature_delta", "volatile_organic_compounds",
        "volatile_organic_compounds_parts", "voltage", "volume",
        "volume_storage", "volume_flow_rate", "water", "weight",
        "wind_direction", "wind_speed",
    },
    "button": {"identify", "restart", "update"},
    "select": set(),
    "switch": set(),
}

SENSOR_NON_NUMERIC_DEVICE_CLASSES = {"date", "enum", "timestamp", "uptime"}

SENSOR_DEVICE_CLASS_UNITS = {
    "absolute_humidity": {"g/m³", "mg/m³"},
    "apparent_power": {"mVA", "VA", "kVA"},
    "aqi": {None},
    "area": {"mm²", "cm²", "m²", "km²", "in²", "ft²", "yd²", "mi²"},
    "atmospheric_pressure": {"Pa", "hPa", "kPa", "MPa", "bar", "cbar", "mbar", "mmHg", "inHg", "psi", "inH₂O"},
    "battery": {"%"},
    "blood_glucose_concentration": {"mg/dL", "mmol/L"},
    "carbon_monoxide": {"ppb", "ppm", "mg/m³", "μg/m³"},
    "carbon_dioxide": {"ppm"},
    "conductivity": {"S/cm", "mS/cm", "μS/cm"},
    "current": {"A", "mA", "μA"},
    "data_rate": {"bit/s", "kB/s", "KB/s", "MB/s", "GB/s", "TB/s", "Mbit/s", "Gbit/s", "Tbit/s", "kbit/s"},
    "data_size": {"bit", "kbit", "Mbit", "Gbit", "Tbit", "B", "kB", "KB", "MB", "GB", "TB", "KiB", "MiB", "GiB", "TiB"},
    "date": set(),
    "distance": {"mm", "cm", "m", "km", "in", "ft", "yd", "mi"},
    "duration": {"d", "h", "min", "s", "ms", "μs"},
    "energy": {"J", "kJ", "MJ", "GJ", "mWh", "Wh", "kWh", "MWh", "GWh", "TWh", "cal", "kcal", "Mcal", "Gcal"},
    "energy_distance": {"kWh/100km", "Wh/km", "mi/kWh", "km/kWh"},
    "energy_storage": {"J", "kJ", "MJ", "GJ", "mWh", "Wh", "kWh", "MWh", "GWh", "TWh", "cal", "kcal", "Mcal", "Gcal"},
    "enum": set(),
    "frequency": {"mHz", "Hz", "kHz", "MHz", "GHz"},
    "gas": {"CCF", "ft³", "m³", "L", "MCF"},
    "humidity": {"%"},
    "illuminance": {"lx"},
    "irradiance": {"W/m²", "BTU/(h⋅ft²)"},
    "moisture": {"%"},
    "monetary": set(),
    "nitrogen_dioxide": {"ppb", "ppm", "μg/m³"},
    "nitrogen_monoxide": {"ppb", "μg/m³"},
    "nitrous_oxide": {"μg/m³"},
    "ozone": {"ppb", "ppm", "μg/m³"},
    "ph": {None},
    "pm1": {"μg/m³"},
    "pm10": {"μg/m³"},
    "pm25": {"μg/m³"},
    "pm4": {"μg/m³"},
    "power": {"mW", "W", "kW", "MW", "GW", "TW"},
    "power_factor": {"%", None},
    "precipitation": {"mm", "cm", "in"},
    "precipitation_intensity": {"mm/d", "mm/h", "in/d", "in/h"},
    "pressure": {"Pa", "hPa", "kPa", "MPa", "bar", "cbar", "mbar", "mmHg", "inHg", "psi", "inH₂O"},
    "reactive_energy": {"varh", "kvarh"},
    "reactive_power": {"mvar", "var", "kvar"},
    "signal_strength": {"dB", "dBm"},
    "sound_pressure": {"dB", "dBA"},
    "speed": {"mm/d", "mm/h", "m/s", "km/h", "mm/s", "in/d", "in/h", "in/s", "ft/s", "mph", "kn", "Beaufort"},
    "sulphur_dioxide": {"ppb", "μg/m³"},
    "temperature": {"°C", "°F", "K"},
    "temperature_delta": {"°C", "°F", "K"},
    "timestamp": set(),
    "uptime": set(),
    "volatile_organic_compounds": {"μg/m³", "mg/m³"},
    "volatile_organic_compounds_parts": {"ppb", "ppm"},
    "voltage": {"V", "mV", "μV", "kV", "MV"},
    "volume": {"mL", "L", "m³", "ft³", "CCF", "MCF", "fl. oz.", "gal"},
    "volume_storage": {"mL", "L", "m³", "ft³", "CCF", "MCF", "fl. oz.", "gal"},
    "volume_flow_rate": {"m³/h", "m³/min", "m³/s", "L/h", "L/min", "L/s", "mL/s", "ft³/min", "gal/min", "gal/d"},
    "water": {"CCF", "ft³", "m³", "L", "MCF", "gal"},
    "weight": {"μg", "mg", "g", "kg", "oz", "lb"},
    "wind_direction": {"°"},
    "wind_speed": {"m/s", "km/h", "ft/s", "mph", "kn", "Beaufort"},
}

SENSOR_DEVICE_CLASS_STATE_CLASSES = {
    "absolute_humidity": {"measurement"},
    "apparent_power": {"measurement"},
    "aqi": {"measurement"},
    "area": {"measurement", "total", "total_increasing", "measurement_angle"},
    "atmospheric_pressure": {"measurement"},
    "battery": {"measurement"},
    "blood_glucose_concentration": {"measurement"},
    "carbon_monoxide": {"measurement"},
    "carbon_dioxide": {"measurement"},
    "conductivity": {"measurement"},
    "current": {"measurement"},
    "data_rate": {"measurement"},
    "data_size": {"measurement", "total", "total_increasing", "measurement_angle"},
    "date": set(),
    "distance": {"measurement", "total", "total_increasing", "measurement_angle"},
    "duration": {"measurement", "total", "total_increasing", "measurement_angle"},
    "energy": {"total", "total_increasing"},
    "energy_distance": {"measurement"},
    "energy_storage": {"measurement"},
    "enum": set(),
    "frequency": {"measurement"},
    "gas": {"total", "total_increasing"},
    "humidity": {"measurement"},
    "illuminance": {"measurement"},
    "irradiance": {"measurement"},
    "moisture": {"measurement"},
    "monetary": {"total"},
    "nitrogen_dioxide": {"measurement"},
    "nitrogen_monoxide": {"measurement"},
    "nitrous_oxide": {"measurement"},
    "ozone": {"measurement"},
    "ph": {"measurement"},
    "pm1": {"measurement"},
    "pm10": {"measurement"},
    "pm25": {"measurement"},
    "pm4": {"measurement"},
    "power": {"measurement"},
    "power_factor": {"measurement"},
    "precipitation": {"measurement", "total", "total_increasing", "measurement_angle"},
    "precipitation_intensity": {"measurement"},
    "pressure": {"measurement"},
    "reactive_energy": {"total", "total_increasing"},
    "reactive_power": {"measurement"},
    "signal_strength": {"measurement"},
    "sound_pressure": {"measurement"},
    "speed": {"measurement"},
    "sulphur_dioxide": {"measurement"},
    "temperature": {"measurement"},
    "temperature_delta": {"measurement"},
    "timestamp": set(),
    "uptime": set(),
    "volatile_organic_compounds": {"measurement"},
    "volatile_organic_compounds_parts": {"measurement"},
    "voltage": {"measurement"},
    "volume": {"total", "total_increasing"},
    "volume_storage": {"measurement"},
    "volume_flow_rate": {"measurement"},
    "water": {"total", "total_increasing"},
    "weight": {"measurement", "total", "total_increasing", "measurement_angle"},
    "wind_direction": {"measurement_angle"},
    "wind_speed": {"measurement"},
}

STATE_CLASS_UNITS = {
    "measurement_angle": {"°"},
}

NUMBER_DEVICE_CLASS_UNITS = dict(SENSOR_DEVICE_CLASS_UNITS)

BINARY_SENSOR_DEVICE_CLASSES = {
    "battery", "battery_charging", "carbon_monoxide", "cold",
    "connectivity", "door", "garage_door", "gas", "heat",
    "light", "lock", "moisture", "motion", "moving",
    "occupancy", "opening", "plug", "power", "presence",
    "problem", "running", "safety", "smoke", "sound",
    "tamper", "update", "vibration", "window",
}
CATEGORIES = {
    "common": (0x0000, 0x0FFF),
    "refrigeration": (0x1000, 0x1FFF),
    "laundry": (0x2000, 0x2FFF),
    "dishwasher": (0x3000, 0x3FFF),
    "waterheater": (0x4000, 0x4FFF),
    "range": (0x5000, 0x5FFF),
    "airconditioning": (0x7000, 0x7FFF),
    "waterfilter": (0x8000, 0x8FFF),
    "smallappliance": (0x9000, 0x9FFF),
    "energy": (0xD000, 0xDFFF),
}

CATEGORIES_LIST = list(CATEGORIES.keys())

PROTOCOL_MARKERS = {"Request Consumed", "Request processed", "Consumed"}
