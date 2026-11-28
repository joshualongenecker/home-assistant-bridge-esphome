# ADR-0019: A board that is silent at one boot keeps its Home Assistant entities

Status: accepted (2026-11-27)
Related: ADR-0008, ADR-0011, issue #212

## Context

Home Assistant discovery publishes a config for an ERD when the bridge has a
cached value for it. A secondary board that does not answer at a particular boot
never reaches the cache, so its ERDs look like they disappeared. Because the
valid-ERD fingerprint and the publish filter both followed the cache, one quiet
boot produced a cleanup + republish that **deleted that board's entities from
Home Assistant**, and the next successful boot re-added them. The loss was
visible only to an external monitor: the run itself completed and logged success
(issue #212).

## Decision

- **The set of boards discovery publishes for comes from the configuration, not
  from the cache.** A board's presence in the fingerprint changes only when the
  configuration does; a configuration change that adds or removes a board still
  changes the fingerprint, as it should.
- **Configured board ERDs are published whether or not they are cached.** The
  bridge lends HA discovery the configured custom ERDs
  (`ha_discovery_manager_set_configured_board_erds()`, kept across `init()`), and
  the publish path keeps a line whose board did not answer, so Home Assistant
  keeps the entity instead of removing it.
- **A run reports the shortfall instead of hiding it.** The completion log
  compares how many configs the republish put back against how many the cleanup
  took down — the cleanup removes every retained config under the device, i.e.
  the previous run's set, so a shortfall is entities Home Assistant is about to
  delete. The run also logs how many published configs are configured board ERDs
  with no cached value, which is the tell that a board is silent.
- **The custom-ERD array is one buffer with two views:** lent to HA discovery as
  `ha_discovery_board_erd_t` and to the poll-list builder as `probe_entry_t`, so
  the three layouts must stay identical.

## Consequences

- A transient board failure no longer destroys user-visible entities; the
  appliance keeps its dashboard and automations through a bad boot.
- Silence is now diagnosable from the run log and from the fleet monitor rather
  than inferred from missing entities.
- The publish filter can no longer use cache presence as a proxy for "the
  appliance has this ERD" when the ERD is configured; the configured set is the
  authority.
- Changing either struct's layout is a cross-module change: the poll-list builder
  and the discovery publisher read the same bytes.

## Code anchors

- `geappliances_bridge.cpp` — `valid_erds_fingerprint_()`, the custom-ERD lend
  call; `geappliances_bridge.h` — the custom ERD array and its two views.
- `ha_discovery_manager.cpp` / `.h` —
  `ha_discovery_manager_set_configured_board_erds()`, `configured_board_erds[]`,
  the publish filter and the completion log.
- `erd_poll_list_builder.h` — `probe_entry_t`.
- `test/tests/startup_integration_test.cpp` — the fingerprint-follows-the-valid-ERD-set
  test.
