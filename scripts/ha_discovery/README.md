# HA Discovery Pipeline

Scripts to convert GE Appliances ERD definitions into Home Assistant MQTT Discovery JSONL files, then compress them into a C header for embedding in ESPHome firmware.

## Pipeline Overview

```
appliance_api_erd_definitions.json (submodule)
    |
    v
generate_flattened_review.py  -->  appliance_api_erd_definitions_processed.json (in this dir)
    |
    v
auto_detect_ha_domain.py      -->  populate ha_domain
auto_detect_device_class.py   -->  populate device_class
auto_detect_state_class.py    -->  populate state_class
auto_detect_scaling.py        -->  infer scaling_factor
auto_detect_pairings.py       -->  detect Request/Status pairs
post_process.py               -->  fix cross-field consistency
    |
    v
generate_ha_discovery.py      -->  ha_discovery/*.jsonl
    |
    v
compress_ha_discovery.py      -->  components/geappliances_bridge/ha_discovery_data.h
```

## Running the Pipeline

```bash
python3 scripts/ha_discovery/run_pipeline.py
```

**Always run this before committing changes to the processed JSON, generator scripts, or overrides.** It regenerates all derived artifacts: the processed JSON (flattened from the `lib/public-appliance-api-documentation` submodule, preserving existing review data), the JSONL files, and the compressed headers, so ESPHome builds use the latest data.

The submodule must be checked out: `git submodule update --init`.

## Directory Structure

- `pipeline/` - Auto-detection scripts
  - `auto_detect_ha_domain.py` - Assign HA domain based on field type/name
  - `auto_detect_device_class.py` - Assign device_class based on keywords
  - `auto_detect_state_class.py` - Assign state_class for sensors
  - `auto_detect_scaling.py` - Infer scaling_factor from field name patterns
  - `auto_detect_pairings.py` - Detect Request/Status ERD pairs
  - `post_process.py` - Fix cross-field consistency issues
  - `ha_constants.py` - HA domain/device_class constants and mappings

- `generators/` - Code generation scripts
  - `generate_flattened_review.py` - Flatten ERDs to one entry per sub-field
  - `generate_ha_discovery.py` - Generate HA discovery JSONL files
  - `compress_ha_discovery.py` - Compress JSONL into C header

- `run_pipeline.py` - Orchestrator script to run the full pipeline

## Output

All generated files are committed to the repo:

- `ha_discovery/*.jsonl` - Category-specific JSONL files
- `components/geappliances_bridge/ha_discovery_data.h` - Compressed C header

During build, ESPHome uses the committed `ha_discovery_data.h` directly - no generation is needed at build time.