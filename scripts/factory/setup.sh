#!/usr/bin/env bash
# One-time factory setup: create the triage state-machine labels on the
# canonical repo. Requires an authenticated account with triage (or higher)
# permission on that repo — run as the repo owner, not the fork account.
#
# Usage:  bash scripts/factory/setup.sh
#         FACTORY_REPO=owner/repo bash scripts/factory/setup.sh   # override target
set -euo pipefail

REPO="${FACTORY_REPO:-joshualongenecker/home-assistant-bridge-esphome}"

create() {
  gh label create --repo "$REPO" "$1" --color "$2" --description "$3" --force
}

create needs-triage        FBCA04 "Awaiting triage into a workable ticket"
create needs-info          D876E3 "Waiting on the reporter for details"
create accepted            0E8A16 "Triaged and accepted for work"
create ready-for-agent     1D76DB "Spec/tickets done; ready for an agent session"
create in-progress         F9D015 "An agent or human is actively working it"
create needs-device-verify 1BAAA8 "Firmware-facing; requires test-bench verification before PR"

echo "Labels created on $REPO."
