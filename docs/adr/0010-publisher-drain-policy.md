# ADR-0010: The ERD publisher paces only large backlogs, backs off reactively on a drop, and re-drains after a long outage

Status: accepted (2026-11-27)
Related: ADR-0004, ADR-0005, issue #209

## Context

ESPHome's MQTT backend has a 30-slot outbound queue and exposes no depth
accessor, so overflow can only be observed as a failed publish. Retrying a
dropped publish at main-loop rate churns the backend's drop counter and flaps
the MQTT component warning. On the other side, a full cache drain at startup
(150–250 configs) can flood the same queue, and a broker restart that loses its
in-memory retained store leaves Home Assistant with no values until each ERD
changes again.

## Decision

Three separate policies, each with its own constant in
`erd_cache_mqtt_publisher.cpp`:

- **Reactive backoff after a drop** — `PUBLISH_RETRY_BACKOFF_MS` (250 ms).
  After a publish is dropped, attempts are suppressed until
  `retry_backoff_until_ms`. The "no window armed" case is `0` and is tested
  first, because the rollover-safe signed diff reads `now - 0` as negative once
  `millis()` passes 2^31, which would otherwise block every publish.
  `on_connected()` clears the window so a fresh connection is not held back by a
  stale backoff.
- **Proactive pacing only above a backlog threshold** —
  `PUBLISH_PACE_BACKLOG_THRESHOLD` (10 dirty entries) and
  `PUBLISH_PACE_INTERVAL_MS` (100 ms). Below the threshold the task is woken
  every loop, so ordinary one-or-two-value changes keep their latency. The first
  paced interval is measured from the first pacing signal, not from boot, so a
  backlog forming inside the first 100 ms of uptime is not suppressed.
  `dirty_count` (ADR-0004) is what makes the backlog cheap enough to consult on
  every signal.
- **Full re-drain after a long outage** — `RECONNECT_REPUBLISH_THRESHOLD_MS`
  (60 000 ms). A disconnect at least that long calls
  `erd_cache_mark_all_updated()`, which covers broker restarts that lose their
  retained store before the next autosave, while avoiding a full flush on every
  short Wi-Fi flap. `disconnect_start_ms` is **not** reset on each reconnect
  attempt: the outage duration is cumulative across ESPHome's retry attempts,
  and the field is reset only on the final successful reconnect.

## Consequences

- Ordinary value changes stay fast; only bulk drains are slowed, and the pacing
  cost is bounded by the threshold.
- Overflow handling stays reactive because there is no queue-depth API; if
  ESPHome ever exposes one, the reactive backoff is the thing to replace.
- A long outage self-heals the broker's retained values without an operator
  action, at the cost of one full drain.
- The backoff and pacing windows are `millis()`-based, so any new window must
  keep the `0 == disabled` check ahead of the signed diff.

## Code anchors

- `erd_cache_mqtt_publisher.cpp` — `PUBLISH_RETRY_BACKOFF_MS`,
  `PUBLISH_PACE_BACKLOG_THRESHOLD`, `PUBLISH_PACE_INTERVAL_MS`,
  `RECONNECT_REPUBLISH_THRESHOLD_MS`, `signal_work()`, `publish_one_()`,
  `on_connected()`, `disconnect_start_ms`, `retry_backoff_until_ms`,
  `last_work_signal_ms`.
- `erd_cache.cpp` — `erd_cache_mark_all_updated()`, `erd_cache_get_dirty_count()`
  (ADR-0004).
- `test/tests/erd_cache_mqtt_publisher_test.cpp`,
  `erd_cache_mqtt_publisher_pause_test.cpp`.
