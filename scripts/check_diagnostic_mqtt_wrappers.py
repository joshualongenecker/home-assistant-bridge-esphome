#!/usr/bin/env python3
"""Assert every component-created diagnostic entity is published over MQTT.

ESPHome publishes an entity on MQTT only when codegen emits an ``MQTTSensorComponent`` /
``MQTTButtonComponent`` wrapper for it, and it emits that wrapper only for entities whose
validated config carries an ``mqtt_id`` (supplied by the ``OnlyWith(CONF_MQTT_ID, "mqtt")``
schema default). Entities assembled by hand in the component's Python skipped validation, so
they were registered, called ``publish_state()`` every cycle, and never reached the broker —
with no test and no compile error to show it.

Run after ``esphome compile docs/test-compile.yaml`` (see .github/workflows/esphome-compile.yml).
"""

from __future__ import annotations

import difflib
import re
import sys
from pathlib import Path

DEFAULT_MAIN_CPP = Path("docs/.esphome/build/gea-esphome-test/src/main.cpp")

# Display names of entities the component creates itself. Mirrors the *_sensor /
# discovery_refresh_button options in components/geappliances_bridge/__init__.py.
EXPECTED_SENSORS = (
    "ERD Publish Rate",
    "ERD Cache Entries",
    "ERD Cache Update Rate",
    "MQTT Publish Rate",
    "MQTT Disconnect Count",
    "MQTT Last Disconnect Duration",
)
EXPECTED_BUTTONS = ("Discovery Refresh",)

REGISTRATION_RE = re.compile(r"App\.register_(sensor|button)\((\w+), \"([^\"]+)\"")
WRAPPER_RE = re.compile(r"new\(\w+\) mqtt::MQTT(Sensor|Button)Component\((\w+)\)")


def main(argv: list[str]) -> int:
    path = Path(argv[1]) if len(argv) > 1 else DEFAULT_MAIN_CPP
    if not path.is_file():
        print(f"error: generated source not found: {path}", file=sys.stderr)
        print("hint: run `esphome compile docs/test-compile.yaml` first", file=sys.stderr)
        return 2

    source = path.read_text(encoding="utf-8", errors="replace")
    # findall() would yield (kind, var); keep (var, kind) to match the lookups below.
    wrapped = {(m.group(2), m.group(1)) for m in WRAPPER_RE.finditer(source)}
    registered = {
        (kind, name): var
        for kind, var, name in REGISTRATION_RE.findall(source)
    }

    names_by_kind = {"sensor": [], "button": []}
    for (kind, name) in registered:
        names_by_kind[kind].append(name)

    expected_list = {"sensor": "EXPECTED_SENSORS", "button": "EXPECTED_BUTTONS"}

    def missing_registration(kind: str, name: str) -> str:
        """Describe an expected entity that has no registration at all.

        A renamed display name lands here too, and the fix for that is the EXPECTED_*
        list, not the component -- so name the closest registered entity instead of
        sending the reader to the schema.
        """
        near = difflib.get_close_matches(name, names_by_kind[kind], n=1, cutoff=0.6)
        if near:
            return (f'{kind} "{name}" is not registered at all, but "{near[0]}" is; if the '
                    f'display name changed on purpose, update {expected_list[kind]}')
        return f'{kind} "{name}" is not registered at all'

    missing = []
    for name in EXPECTED_SENSORS:
        var = registered.get(("sensor", name))
        if var is None:
            missing.append(missing_registration("sensor", name))
        elif (var, "Sensor") not in wrapped:
            missing.append(f'sensor "{name}" ({var}) has no MQTTSensorComponent wrapper')
    for name in EXPECTED_BUTTONS:
        var = registered.get(("button", name))
        if var is None:
            missing.append(missing_registration("button", name))
        elif (var, "Button") not in wrapped:
            missing.append(f'button "{name}" ({var}) has no MQTTButtonComponent wrapper')

    if missing:
        print("Diagnostic entities will not appear in Home Assistant:", file=sys.stderr)
        for line in missing:
            print(f"  - {line}", file=sys.stderr)
        print(
            "\nAn entity is only published when its config carries mqtt_id, which the entity\n"
            "schema supplies. Build these entities through sensor.sensor_schema() /\n"
            "button.button_schema() (see DIAGNOSTIC_ENTITY_KEYS) instead of assembling the\n"
            "config dict by hand.",
            file=sys.stderr,
        )
        return 1

    print(f"OK: {len(EXPECTED_SENSORS) + len(EXPECTED_BUTTONS)} diagnostic entities have MQTT wrappers")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
