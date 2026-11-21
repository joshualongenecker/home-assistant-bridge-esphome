#!/usr/bin/env python3
"""Assert the GEA_BRIDGE_HAS_* defines track the configured UARTs.

``protocol_stack.h`` compiles a protocol's ring buffers out when its define is
absent (8447 B of static storage for GEA2, 5351 B for GEA3), and
``ProtocolStack::init()`` nulls the matching UART pointer in that build. That is
only safe while ``__init__.py`` emits the define exactly when it also calls
``set_gea3_uart`` / ``set_gea2_uart``:

- define without a UART  -> buffers reserved for nothing (wasted RAM, no bug),
- UART without a define  -> a configured protocol silently disabled, because
  init() nulls the pointer the runtime gate (has_gea2_uart) reads.

The second is the case this check exists to prevent.

Usage: check_protocol_buffer_defines.py <esphome build dir> [...]

One argument per config, the directory ``esphome compile`` creates for it (for
example ``docs/.esphome/build/gea-esphome-test``). ``cg.add_define()`` lands in
``src/esphome/core/defines.h`` and the UART wiring in ``src/main.cpp``, so both
files of each build dir are read. See .github/workflows/esphome-compile.yml.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

DEFINES_REL = "src/esphome/core/defines.h"
MAIN_REL = "src/main.cpp"

# (define emitted by to_code, setter call emitted for the same UART)
PROTOCOLS = (
    ("GEA_BRIDGE_HAS_GEA3", "set_gea3_uart"),
    ("GEA_BRIDGE_HAS_GEA2", "set_gea2_uart"),
)


def check(build_dir: Path) -> list[str]:
    """Return the problems found in one generated build dir (empty == clean)."""
    defines_path = build_dir / DEFINES_REL
    main_path = build_dir / MAIN_REL
    if not defines_path.is_file() or not main_path.is_file():
        return [
            f"{build_dir}: generated sources not found "
            f"(expected {DEFINES_REL} and {MAIN_REL}; run `esphome compile --only-generate` first)"
        ]

    defines = defines_path.read_text(encoding="utf-8", errors="replace")
    main = main_path.read_text(encoding="utf-8", errors="replace")
    problems: list[str] = []
    emitted = set()

    for define, setter in PROTOCOLS:
        has_define = re.search(rf"^#\s*define\s+{define}\b", defines, re.MULTILINE) is not None
        has_uart = re.search(rf"(?:\.|->)\s*{setter}\s*\(", main) is not None
        if has_define:
            emitted.add(define)
        if has_define and not has_uart:
            problems.append(f"{build_dir}: {define} defined but {setter}() is never called")
        if has_uart and not has_define:
            problems.append(f"{build_dir}: {setter}() called but {define} is not defined")

    # The component schema rejects a config with neither UART, so an ESPHome
    # build always defines at least one protocol. Neither define means the
    # header's "both stacks" fallback silently applies to a real device.
    if not emitted:
        problems.append(
            f"{build_dir}: neither GEA_BRIDGE_HAS_GEA3 nor GEA_BRIDGE_HAS_GEA2 is defined; "
            "the component would compile both protocol stacks"
        )

    return problems


def main(argv: list[str]) -> int:
    build_dirs = [Path(arg) for arg in argv[1:]]
    if not build_dirs:
        print(f"usage: {Path(argv[0]).name} <esphome build dir> [...]", file=sys.stderr)
        return 2

    problems: list[str] = []
    for build_dir in build_dirs:
        problems.extend(check(build_dir))

    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1

    for build_dir in build_dirs:
        print(f"ok: {build_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
