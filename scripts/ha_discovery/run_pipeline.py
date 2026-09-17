#!/usr/bin/env python3
"""Regenerate all HA discovery artifacts in sequence.

Usage:
    python3 scripts/ha_discovery/run_pipeline.py

Steps:
    1. Flatten ERD definitions from the lib/public-appliance-api-documentation
       submodule into the processed JSON, preserving existing review data.
    2. Run auto_detect_pairings on the processed JSON (in-place).
    3. Run auto_detect_ha_domain on the processed JSON (in-place).
    4. Run auto_detect_device_class on the processed JSON (in-place).
    5. Run auto_detect_scaling on the processed JSON (in-place).
    6. Run auto_detect_state_class on the processed JSON (in-place).
    7. Post-process (reapply overrides).
    8. Generate JSONL files to ha_discovery/.
    9. Compress JSONL into ha_discovery_data.h.

Requires the lib/public-appliance-api-documentation submodule to be checked
out (git submodule update --init).

Idempotency: the pipeline is safe to re-run. Auto-detection scripts may
clear stale values (e.g. device_class for fields that no longer match),
and post_process re-applies all overrides at the end. Running the pipeline
multiple times produces the same result as a single run.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent.parent
    generators = script_dir / "generators"
    pipeline = script_dir / "pipeline"
    processed = script_dir / "appliance_api_erd_definitions_processed.json"
    ha_dir = repo_root / "ha_discovery"
    submodule = repo_root / "lib" / "public-appliance-api-documentation"
    erd_definitions = submodule / "appliance_api_erd_definitions.json"
    appliance_api = submodule / "appliance_api.json"

    def run(cmd, step_name):
        print(f"  {' '.join(str(c) for c in cmd)}", file=sys.stderr)
        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError as e:
            print(f"ERROR: {step_name} failed with exit code {e.returncode}", file=sys.stderr)
            raise

    # Helper: run a pipeline script with atomic write (temp file + rename).
    def run_pipeline_script(script_name, step_name):
        script = pipeline / script_name
        # Write to a temp file in the same directory, then atomically rename.
        fd, tmp_path = tempfile.mkstemp(
            suffix='.json', dir=str(processed.parent), prefix='.tmp_' + processed.name
        )
        os.close(fd)
        try:
            run(
                [sys.executable, str(script),
                 "--input", str(processed), "--output", tmp_path],
                step_name,
            )
            shutil.move(tmp_path, str(processed))
        except Exception:
            # Clean up temp file on failure.
            try:
                Path(tmp_path).unlink(missing_ok=True)
            except OSError:
                pass
            raise

    # Step 1: Flatten ERD definitions from the submodule (preserving reviews)
    print("Step 1: Flatten ERD definitions from submodule...", file=sys.stderr)
    if not erd_definitions.exists() or not appliance_api.exists():
        print(f"ERROR: submodule JSON not found under {submodule}", file=sys.stderr)
        print("Run 'git submodule update --init' and retry.", file=sys.stderr)
        sys.exit(1)
    # Atomic write: temp file + rename, like the other in-place steps.
    fd, tmp_path = tempfile.mkstemp(
        suffix='.json', dir=str(processed.parent), prefix='.tmp_' + processed.name
    )
    os.close(fd)
    try:
        cmd = [sys.executable, str(generators / "generate_flattened_review.py"),
               "--input", str(erd_definitions),
               "--api", str(appliance_api),
               "--output", tmp_path]
        if processed.exists():
            cmd += ["--preserve-review", str(processed)]
        run(cmd, "generate_flattened_review")
        shutil.move(tmp_path, str(processed))
    except Exception:
        # Clean up temp file on failure.
        try:
            Path(tmp_path).unlink(missing_ok=True)
        except OSError:
            pass
        raise

    # Step 2: Auto-detect pairings
    print("Step 2: Auto-detect pairings...", file=sys.stderr)
    run_pipeline_script("auto_detect_pairings.py", "auto_detect_pairings")

    # Step 3: Auto-detect ha_domain
    print("Step 3: Auto-detect ha_domain...", file=sys.stderr)
    run_pipeline_script("auto_detect_ha_domain.py", "auto_detect_ha_domain")

    # Step 4: Auto-detect device_class
    print("Step 4: Auto-detect device_class...", file=sys.stderr)
    run_pipeline_script("auto_detect_device_class.py", "auto_detect_device_class")

    # Step 5: Auto-detect scaling
    print("Step 5: Auto-detect scaling...", file=sys.stderr)
    run_pipeline_script("auto_detect_scaling.py", "auto_detect_scaling")

    # Step 6: Auto-detect state_class
    print("Step 6: Auto-detect state_class...", file=sys.stderr)
    run_pipeline_script("auto_detect_state_class.py", "auto_detect_state_class")

    # Step 7: Post-process (reapply overrides)
    print("Step 7: Post-process...", file=sys.stderr)
    run_pipeline_script("post_process.py", "post_process")

    # Step 8: Generate JSONL
    print("Step 8: Generate JSONL...", file=sys.stderr)
    run([sys.executable, str(generators / "generate_ha_discovery.py"),
         "--processed", str(processed),
         "--output-dir", str(ha_dir)], "generate_ha_discovery")

    # Step 9: Compress into ha_discovery_data.h
    print("Step 9: Compress header...", file=sys.stderr)
    run([sys.executable, str(generators / "compress_ha_discovery.py"),
         "--input-dir", str(ha_dir),
         "--header-name", "ha_discovery_data"], "compress_ha_discovery")

    print("Done!", file=sys.stderr)


if __name__ == "__main__":
    main()