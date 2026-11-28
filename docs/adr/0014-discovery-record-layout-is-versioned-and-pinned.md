# ADR-0014: The NVS discovery record is a pinned, versioned layout, and a flapping valid-ERD set cannot republish forever

Status: accepted (2026-11-27)
Related: ADR-0002, ADR-0003, ADR-0008

## Context

After each successful discovery publish the bridge stores a `DiscoveryRecord` in
NVS and compares it on the next boot to decide whether Home Assistant's retained
configs need cleanup and republish. That record is written by one firmware
version and read by the next one after an OTA. ESPHome will not load a blob of a
different size as the current struct, so a layout change silently turns "the
device has a record" into "the device has no record" — which changes behaviour
from *cleanup then republish* to *republish without cleanup*, leaving orphaned
configs on the broker.

A second problem is specific to the valid-ERD fingerprint (ADR-0008): an
appliance whose feature-bit reads are partial can report a different ERD set on
every boot. Each change triggers a cleanup, a republish, and — because a
republish ends in a reboot — another attempt.

## Decision

- **Field order and sizes are the on-NVS layout and are pinned by
  `static_assert`.** Changing any of them requires bumping `kRecordVersion` so
  old records are republished exactly once. A failing `static_assert` means the
  record changed intentionally (bump the version) or accidentally (fix the
  field).
- **New fields go into the previous version's padding, so the old layout stays a
  byte-exact prefix of the new one**:
  `static_assert(offsetof(DiscoveryRecord, valid_erds_republishes) == kRecordV4Size - 2)`.
- **The loader decodes either layout.** A blob of `sizeof(DiscoveryRecord)` or of
  `kRecordV4Size` (whose version field stays 4) decodes; the newer fields stay 0,
  and the change check then reports a stale version, so an upgraded device does a
  cleanup + republish instead of a bare publish. Any other size is treated as no
  record.
- **Version history:** v3 added the persisted discovery-affecting config flags,
  v4 added the availability topic to every payload, v5 added the valid-ERD
  fingerprint. Records older than the oldest decodable layout have no compatible
  combined custom-hash/config state and are treated as absent.
- **A flapping valid-ERD fingerprint is bounded.** After
  `kMaxValidErdsRepublishes` (2) republishes in a row caused only by the
  fingerprint, further fingerprint changes are logged and ignored until the
  fingerprint has been seen unchanged at `kValidErdsStableChecksToReset` (3)
  change checks in a row. A flapping set never reaches that, because every
  mismatch restarts the streak. Discovery Refresh or any other discovery change
  resets the budget. The two counters live in what was v4's padding, so bounding
  the flap cost no layout break.
- **Discovery data generated before hash support still invalidates correctly:**
  the generator hashes the custom discovery file contents at compile time, so
  replacing such a file triggers cleanup and republish on the next boot even
  though the header itself carries no hash field.

## Consequences

- An OTA never silently loses a device's discovery state; the worst case is one
  extra cleanup + republish.
- Layout changes are cheap to get right and loud when wrong, because the prefix
  assertion fails at compile time rather than at boot.
- A device with an unstable feature-bit set republishes at most twice, then goes
  quiet instead of reboot-looping; the operator sees a log line, not a loop.
- Any new record field must fit in existing padding or the version must be
  bumped, and `decode_record` must keep accepting the previous size.

## Code anchors

- `discovery_lifecycle_manager.h` — `DiscoveryRecord`, `DiscoveryRecordV4`,
  `kRecordV4Size`, `kRecordVersion`, `decode_record()`,
  `kMaxValidErdsRepublishes`, `kValidErdsStableChecksToReset`,
  `valid_erds_republishes`, `valid_erds_stable_checks`.
- `discovery_lifecycle_manager.cpp` — `check_discovery_changes()`,
  `set_valid_erds_fingerprint()`, the record read/write helpers.
- `scripts/ha_discovery/` — the custom-discovery-data hash emitted by the
  generator.
- `test/tests/discovery_lifecycle_manager_test.cpp` — record decode and
  fingerprint-budget tests.
