#!/usr/bin/env bash
#
# Verify that generated files are in sync with their generators.
#
# Re-runs the generators and fails if any generated file differs from the
# committed version. This catches two failure modes:
#   1. Hand-edits to generated files (they get overwritten on the next regen).
#   2. Stale generated output (a generator changed but the output was not
#      regenerated).
#
# Generated files and the generator that produces each:
#   scripts/generate_erd_lists.py
#       -> components/geappliances_bridge/erd_lists.{h,cpp}
#       -> components/geappliances_bridge/appliance_api_feature_lists.{h,cpp}
#       -> components/geappliances_bridge/appliance_type_map.h
#   scripts/ha_discovery/run_pipeline.py
#       -> components/geappliances_bridge/ha_discovery_data.{h,cpp}
#       -> components/geappliances_bridge/rmw_table.{h,cpp}
#       -> ha_discovery/*.jsonl
#       -> scripts/ha_discovery/appliance_api_erd_definitions_processed.json
#
# Usage:
#   scripts/check_generated_sync.sh     (or: make check-generated)

set -euo pipefail

cd "$(dirname "$0")/.."

SUBMODULE="lib/public-appliance-api-documentation"
if [ ! -f "$SUBMODULE/appliance_api_erd_definitions.json" ] || [ ! -f "$SUBMODULE/appliance_api.json" ]; then
  echo "ERROR: $SUBMODULE submodule is not checked out." >&2
  echo "Run: git submodule update --init" >&2
  exit 1
fi

# Generated files that must match a fresh generator run.
GENERATED=(
  components/geappliances_bridge/erd_lists.h
  components/geappliances_bridge/erd_lists.cpp
  components/geappliances_bridge/appliance_api_feature_lists.h
  components/geappliances_bridge/appliance_api_feature_lists.cpp
  components/geappliances_bridge/appliance_type_map.h
  components/geappliances_bridge/ha_discovery_data.h
  components/geappliances_bridge/ha_discovery_data.cpp
  components/geappliances_bridge/rmw_table.h
  components/geappliances_bridge/rmw_table.cpp
  scripts/ha_discovery/appliance_api_erd_definitions_processed.json
)
# ha_discovery/*.jsonl are generated too; resolve the set from the index.
mapfile -t JSONL < <(git ls-files 'ha_discovery/*.jsonl')
GENERATED+=("${JSONL[@]}")

echo "Running generators..."
if ! python3 scripts/generate_erd_lists.py > /dev/null 2>&1; then
  echo "ERROR: scripts/generate_erd_lists.py failed. Run it manually to see the error." >&2
  exit 1
fi
if ! python3 scripts/ha_discovery/run_pipeline.py > /dev/null 2>&1; then
  echo "ERROR: scripts/ha_discovery/run_pipeline.py failed. Run it manually to see the error." >&2
  exit 1
fi

# Any change (modified, deleted, or newly-created) to a generated file means
# the committed output is out of sync with the generators.
CHANGED=$(git status --porcelain -- "${GENERATED[@]}" || true)

if [ -n "$CHANGED" ]; then
  echo "" >&2
  echo "ERROR: generated files are out of sync with their generators." >&2
  echo "The following generated files differ from a fresh generator run:" >&2
  echo "$CHANGED" >&2
  echo "" >&2
  echo "Do NOT hand-edit generated files. Fix the source instead:" >&2
  echo "  - the lib/public-appliance-api-documentation submodule, or" >&2
  echo "  - the pipeline overrides in scripts/ha_discovery/pipeline/post_process.py, or" >&2
  echo "  - the generator scripts under scripts/." >&2
  echo "Then regenerate and commit the output:" >&2
  echo "  python3 scripts/generate_erd_lists.py" >&2
  echo "  python3 scripts/ha_discovery/run_pipeline.py" >&2
  exit 1
fi

echo "OK: generated files are in sync with their generators."
