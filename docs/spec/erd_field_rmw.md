# Field-Level Read-Modify-Write (RMW) — Design & Plan

**Status:** Phase 1 implemented (matched-pair RMW). See the generated `rmw_table.{h,cpp}` for the enabled set.
**Issue:** [#156](https://github.com/joshualongenecker/home-assistant-bridge-esphome/issues/156) — Part 2 (the "larger, future" read-modify-write alternative).
**Part 1 (done):** PR #154 / #159 demoted 100 multi-field / partial-write ERDs to read-only so a sub-field command can never clobber or mis-address a sibling. See `demotion_list.md`.

---

## 1. Goal

Make a sub-field of a multi-field ERD writable again by having the bridge perform a **read-modify-write**:

1. **Read** the current ERD payload (from the `erd_cache`).
2. **Modify** only the one sub-field (bytes or bits) the command targets.
3. **Write** the new **complete** payload to the ERD.

This restores control over the 100 ERDs that Part 1 had to demote, without reintroducing the clobber / mis-address bug.

---

## 2. Why the demotion happened (recap)

The bridge writes the MQTT payload **verbatim** to the ERD (`erd_write_bridge.cpp` → `tiny_gea3_erd_client_write(erd, value, size)`). There is no read, no merge, no bit logic. A sub-field `switch`/`number`/`select` whose command renders **fewer bytes than the ERD** therefore:

- **Clobbers** sibling fields when the write is the correct size and lands (e.g. `0x7051` Air Purifier is a 1-byte ERD; toggling "Ozone" writes `01` = bit 0 = "Air Purifier Off", not bit 2).
- **Fails as incorrectly-sized** when the write is narrower than the ERD (the appliance rejects any size mismatch).

Part 1 made every such sub-field read-only. Part 2 fixes the root cause in the bridge.

---

## 3. The RMW mechanism

For a field-level write the bridge does **not** forward the short payload. Instead:

```
field-write to ERD R, field (byte_offset, byte_size, bit_offset, bit_size), value V
  1. base = RMW_table[R].base_erd            # which cached ERD holds the current state
  2. snapshot = erd_cache_snapshot(base)      # full current payload, copied under the cache lock
  3. if snapshot missing / wrong size:
         drop with warning + publish failure result        # safe fallback
  4. patch(snapshot, byte_offset, bit_offset, bit_size, V) # modify only the target field
  5. tiny_gea3_erd_client_write(R, snapshot, len(snapshot))# write the FULL payload
```

The patch is a byte copy when the field is byte-aligned (`bit_offset == 0` and `bit_size % 8 == 0`), and a bit read-modify-write otherwise. The write is always the **full** ERD size, so it is correctly-sized and never mis-addressed.

**The load-bearing requirement:** the base payload must have the **same layout and size** as the write target, so that patching one field yields a valid full payload. That requirement is what determines which demoted ERDs can be fixed (Section 6).

---

## 4. Field-write topic format

A new topic level carries the field geometry. The existing full-write topic is unchanged, so this is backward compatible.

| | Topic |
|---|---|
| **Full write** (existing) | `geappliances/{device_id}/erd/0x{REQ}/write` |
| **Field write** (new) | `geappliances/{device_id}/erd/0x{REQ}/write/{byte_offset}/{byte_size}/{bit_offset}/{bit_size}` |

All four field segments are **decimal** integers:

| Segment | Meaning |
|---|---|
| `byte_offset` | 0-based offset of the field's first byte |
| `byte_size` | number of bytes the field spans |
| `bit_offset` | bit offset within the first byte (`0` for byte-aligned fields) |
| `bit_size` | number of bits in the field (`byte_size*8` for byte-aligned fields) |

The bridge reconstructs the absolute bit range as `[byte_offset*8 + bit_offset,  byte_offset*8 + bit_offset + bit_size)`.

**Examples**

| Field | Topic suffix |
|---|---|
| Air Purifier "Ozone" (byte 0, bit 2, 1 bit) | `…/0x7051/write/0/1/2/1` |
| Cycle Pricing "Price[1]" (bytes 2–3, u16) | `…/0x2164/write/2/2/0/16` |
| Smoker "Cavity Temperature" (bytes 2–3, u16) | `…/0x9401/write/2/2/0/16` |

**Command template.** Each field entity's `command_template` renders **only the field value** as a lowercase hex string, **big-endian (MSB first)** — matching the ERD payload's big-endian byte order (the VTs, the `>f` float32 unpack, and the write API's "Data must already be in big endian" warning all agree). The RMW patch reads the rendered value big-endian and places the field's bits big-endian within the field's `byte_size` span (field bit *i* is bit `bit_offset + i` of the big-endian `byte_size`-byte integer):

- 1-bit switch → `00` / `01` (1 byte).
- N-bit field, N ≤ 8 → 1 byte, zero-extended.
- N-bit field, N > 8 → `N/8` bytes.
- A multi-byte field (e.g. a 3-bit field in a 2-byte span) renders the full `byte_size` bytes big-endian, so the low bits live in the last byte.

**Subscription.** The adapter's wildcard changes from `…/erd/+/write` to `…/erd/+/write/#`. Per the MQTT spec, `#` matches zero or more trailing levels (subscribing to `a/b/#` also receives `a/b`), so this single filter catches both the full-write topic (zero trailing levels) and the field-write topic (four trailing levels). The parser reads the ERD (and optional `0x{addr}_` prefix) exactly as today, then inspects what follows `write`:
- **zero** trailing segments → full write (existing behavior, unchanged).
- **exactly four** decimal segments → field write; fill the field geometry into the write-args struct.
- **any other** shape → drop the message (malformed).

**Secondary boards.** The `0x{addr}_0x{erd}` prefix form works unchanged; the field segments come after `write` in both forms.

---

## 5. Base-selection rules

The base is the cached ERD whose payload is the current state of the write target. Decision tree:

```
For a field-write to request ERD R:
  1. R in RMW_table?
       no  → not an RMW target (treat as a normal full write; should not occur for a field topic)
       yes → base = RMW_table[R].base_erd
  2. base cached in erd_cache with the expected payload size?
       no  → DROP with warning; publish a failure write_result. (safe fallback)
       yes → snapshot, patch, write full payload
```

**Matched-pair flow (Phase 1).** For a matched pair, `base_erd` is the **status** ERD, so the sequence is: **read the current status payload → modify only the target field → write the full payload to the request ERD.** The request ERD is never read as the base (it may be a consumed/`0xFF` value); the status ERD is the source of truth.

Two base sources, chosen by the pipeline and baked into the generated `RMW_table`:

| Base source | When | Why it's valid |
|---|---|---|
| **Paired status ERD** | R and its status ERD have an identical non-reserved field layout **and** equal payload size | The status ERD is polled (cached) and holds the live state; same layout ⇒ patching a field yields a valid request payload. |
| **R's own cached value** | R is a standalone "set-the-value" state ERD (no paired status) that is polled and not a consumed request | R's own latest read is the current state. Requires per-ERD verification (Section 7.3). |

If neither holds, there is **no valid base** and the ERD stays read-only.

**"Request Consumed" caveat.** Many request ERDs are reset by the appliance to `0xFF`/`0x00` after the request is consumed. Such an ERD's own cached value is therefore **not** a usable base — which is exactly why the paired-status base (which is never consumed) is preferred, and why consumed standalone requests stay read-only.

---

## 6. Categorization of the 100 demoted ERDs

Re-analyzing all 100 demoted ERDs against the processed ERD definitions, the demotion *reason* (clobber / multi-writable / single-partial) is secondary. What actually determines RMW feasibility is **whether a valid base exists**:

| Category | Count | Base available? | Fix |
|---|---|---|---|
| **M — matched pair** (request + status identical layout, equal size) | **18** | ✅ paired status ERD | **RMW now (Phase 1)** |
| **D — divergent pair** (request + status layouts differ) | 14 | ⚠️ partial prefix / mask only | per-ERD mask+value or prefix base (Phase 2) |
| **S-state — standalone state ERD** (no pair; holds persistent state) | 36 | ⚠️ own cached value | per-ERD verification, own-value base (Phase 2) |
| **S-req — standalone request ERD** (no pair; consumed / one-shot / delta) | 32 | ❌ none | **stays read-only (Phase 3)** |
| **Total** | **100** | | |

Only the **matched pairs** are provably safe to enable now. The rest need per-ERD analysis and carry a higher risk of a wrong full-payload write.

> **Implementation note (Phase 1):** the ERD definitions grew after this Part-1 analysis, so the current pipeline identifies **125** matched-pair request ERDs (the 18 above are a subset). Only **16** of them contain sub-fields that need field-level RMW — the other 109 are single-field (full-payload) writers that use the regular `.../write` topic and need no RMW. The generated `rmw_table.{h,cpp}` therefore has **16** rows (one per sub-field request ERD). The authoritative enabled set is the generated table.

---

## 7. Per-case plan

### 7.1 Phase 1 — matched pairs (RMW-enabled sub-fields: 16 ERDs)

Request and status ERDs have identical non-reserved field layouts and equal payload size. Base = the paired status ERD (polled, cached). All are "set-the-value" request ERDs, so read-status → patch-field → write-full is correct.

| Request ERD | Name | Base (status) | Payload (B) | Writable fields |
|---|---|---|---|---|
| `0x20b8` | Spin Level Option Request | `0x20b7` | 1 | 1 |
| `0x2164` | Cycle Pricing Request | `0x2165` | 48 | 32 |
| `0x2166` | Option Pricing Request | `0x2167` | 36 | 27 |
| `0x2168` | Mid-Cycle Adjustment Pricing Request | `0x2169` | 28 | 19 |
| `0x222d` | Flexible Dispensing Tank 1 Configuration Request | `0x222c` | 2 | 2 |
| `0x222f` | Flexible Dispensing Tank 2 Configuration Request | `0x222e` | 2 | 2 |
| `0x223c` | Commercial Remote Wash Times Request | `0x223d` | 32 | 32 |
| `0x224a` | Commercial Remote Spin Limit Option Request | `0x224b` | 16 | 16 |
| `0x224c` | Commercial Remote Dry Times Request | `0x224d` | 20 | 20 |
| `0x224f` | Commercial Remote Dry Temperature Request | `0x2250` | 20 | 10 |
| `0x2252` | Commercial Remote Cooldown Times Request | `0x2253` | 10 | 10 |
| `0x2255` | Commercial Remote Cooldown Temperature Request | `0x2256` | 20 | 10 |
| `0x322e` | Dish Favorite Settings Request | `0x322f` | 12 | 9 |
| `0x5023` | Accent Lighting Request | `0x5024` | 12 | 4 |
| `0x7051` | Air Purifier Request | `0x7053` | 1 | 5 |
| `0x7982` | Motion Sensing Mode Selection Request | `0x7983` | 3 | 3 |
| `0x79c1` | Indoor Temperature Chassis/Install Adjustment Request | `0x79c0` | 4 | 1 |
| `0x913f` | Light Color Request | `0x913e` | 4 | 4 |

**Worked example — `0x7051` Air Purifier** (the headline clobber case from the issue). Base = `0x7053`. Each bit becomes an independent field-write:

| Sub-field | Field-write topic suffix |
|---|---|
| Air Purifier Off (bit 0) | `…/0x7051/write/0/1/0/1` |
| Ultraviolet sterilization (bit 1) | `…/0x7051/write/0/1/1/1` |
| Ozone (bit 2) | `…/0x7051/write/0/1/2/1` |
| Negative ions (bit 3) | `…/0x7051/write/0/1/3/1` |
| PM2.5 (bit 4) | `…/0x7051/write/0/1/4/1` |

Toggling "Ozone" on: read `0x7053` (say `05` = Off+Ozone), set bit 2 → `05`, write `05` to `0x7051`. The other four bits are preserved — no clobber.

The full per-field topic suffixes for all enabled ERDs are **derived by the pipeline** (offset/size/bits are already in the processed definitions); they are not hand-maintained. The table above lists the 18 from the original Part-1 analysis; the current pipeline enables **16** sub-field request ERDs for RMW (the authoritative set is the generated `rmw_table.{h,cpp}`).

### 7.2 Phase 2a — 14 divergent pairs (need per-ERD analysis)

Request and status layouts differ, so the status payload is **not** a drop-in base. Two sub-patterns:

| Request ERD | Name | Base | Pattern | Notes |
|---|---|---|---|---|
| `0x1306` | Grow State Request | `0x1307` | mask+value | 1 mask field |
| `0x9401` | Smoker Cooking Request | `0x9400` | mask+value | 8 "Requested Parameter Mask" bits |
| `0x1315` | Paused Mode Request | `0x1327` | prefix | pfx=1 |
| `0x2316` | Commercial Remote Max Drying Times Request | `0x2317` | prefix | pfx=10 |
| `0x2f07` | Mabe - Loads Request | `0x2f08` | none | pfx=0 |
| `0x511c` | Upper Oven Cook Time Add Or Subtract Request | `0x511d` | prefix | pfx=1 |
| `0x5121` | Upper Cavity Cook Recipe Request | `0x5120` | prefix | pfx=1 |
| `0x521c` | Lower Oven Cook Time Add Or Subtract Request | `0x521d` | prefix | pfx=1 |
| `0x5221` | Lower Cavity Cook Recipe Request | `0x5220` | prefix | pfx=1 |
| `0x5402` | Advantium Cook Request | `0x5401` | prefix | pfx=3 |
| `0x5416` | Advantium Remote JSON Cook Request | `0x5417` | prefix | pfx=2 |
| `0x5c3d` | Staged Microwave Cook Request | `0x5c3e` | none | pfx=0 |
| `0xd00b` | Temporary Temperature Offset Demand Response Request | `0xd00c` | prefix | pfx=2 |
| `0xd023` | Electrical Pricing Structure Selection Request | `0xd022` | prefix | pfx=1 |

- **mask+value** (`0x1306`, `0x9401`): the request carries a "Requested Parameter Mask" byte(s) that tells the appliance *which* value fields to apply. The RMW must (a) take the value fields from the status (the matching prefix), (b) patch the target value, and (c) **set only the target's mask bit** and zero the rest. This needs a generated value-field → mask-bit map per ERD. `0x9401` is the canonical example (State/Cook Mode/Cavity Temp/Cook Time/Probe Temp/Smoke Level + a 6-bit request mask).
- **prefix** (10 ERDs): the first N non-reserved fields match, but the writable fields extend beyond the matching prefix (or the request has extra trailing fields). A base can be constructed only if every writable field falls inside a region whose request layout equals the status layout **and** the non-writable trailing bytes have a known-safe value (usually `0x00`/reserved). Each needs individual verification.
- **none** (`0x2f07`, `0x5c3d`): no layout overlap at all — no valid base. **Stays read-only.**

These are **not** enabled in Phase 1. Each is a candidate for Phase 2 only after its base-construction is verified against the appliance protocol.

### 7.3 Phase 2b — 36 standalone "state" ERDs (need per-ERD verification)

No paired status ERD. The only possible base is the ERD's **own** latest cached value, which is valid **only if** the ERD is polled *and* holds a persistent multi-field state (a "set-the-value" ERD). Candidates:

`0x0502 0x100a 0x101a 0x1022 0x1031 0x141f 0x14c8 0x2069 0x5100 0x5124 0x5125 0x5126 0x5127 0x5128 0x5200 0x5224 0x5225 0x5226 0x5227 0x5228 0x5413 0x5423 0x5c0f 0x5c10 0x5c1b 0x5c1e 0x5c24 0x5c28 0x5c2a 0x5c2e 0x5c33 0x5c35 0x900b 0x930a 0x9602 0x9606`

**Important:** this list is a *candidate* set, not a verified one. Several are **one-shot commands or deltas, not persistent state**, and must be excluded:

- `0x5c10` "Add 30 Second Command", `0x5c1b`/`0x5c1e` "…Timer Modification", `0x900b` "Start Brew", `0x930a` "…Time Modification" — the written value is an action/delta, not a state to preserve. RMW is meaningless (there is no sibling state to keep).
- The cook-recipe stage ERDs (`0x5124`–`0x5128`, `0x5224`–`0x5228`) are large (150 B) multi-field tables; they are the strongest own-value-base candidates but need verification that the appliance echoes them back unchanged when polled.

Each ERD must be individually confirmed to be a polled, persistent, set-the-value ERD before it is added to the `RMW_table` with `base = self`.

### 7.4 Phase 3 — 32 standalone request ERDs (stay read-only)

No paired status and no valid own-value base (consumed after write, one-shot, or delta). These **remain read-only**:

`0x0300 0x131f 0x1320 0x1321 0x1322 0x2096 0x20c1 0x20d5 0x2104 0x2143 0x5c11 0x901e 0x904b 0x9052 0x9208 0x920b 0x920d 0x920f 0x9211 0x9213 0x9215 0x9217 0x9219 0x921b 0x921d 0x921f 0x9221 0x9223 0x9227 0x9231 0x9233 0x9235`

(Plus the two divergent "none" ERDs `0x2f07`, `0x5c3d` from 7.2.) These are correctly exposed as `binary_sensor`/`sensor` and need no further action.

---

## 8. Implementation plan

The change spans the discovery pipeline and three C components. It is additive: the full-write path is untouched, and RMW is enabled only for ERDs present in the generated table.

### 8.1 Pipeline — `scripts/ha_discovery/generators/generate_ha_discovery.py`

- In `_demote_partial_write_entities()`, stop demoting an entity when its target ERD is a **verified RMW target** (matched pair, or verified standalone-state). Instead:
  - Keep the domain (`switch`/`number`/`select`).
  - Set `command_template` to render **only the field value** (hex, `ceil(bit_size/8)` bytes).
  - Record the field geometry (`byte_offset`, `byte_size`, `bit_offset`, `bit_size`) on the entry.
- Add a layout-match check (non-reserved field signature + equal payload size) to decide "matched pair".
- Emit a new generated artifact: the **RMW table** (`request_erd → base_erd, payload_size`), one row per enabled ERD.
- Keep the existing regression test and extend it: a generated ERD may expose a writable sub-field **only if** it is in the RMW table (i.e., has a verified base). This replaces the "≤1 writable per byte" rule for RMW targets while still failing closed for anything new.

### 8.2 JSONL / C discovery data

- Add a compact field to the JSONL for RMW entities, e.g. `"fw":"{off}/{size}/{bitoff}/{bitsize}"` (empty for non-RMW entities). The existing short-key convention (`i`,`n`,`d`,`ds`,`vt`,`ct`,…) is preserved.
- `compress_ha_discovery.py` / the C data generator carry the new field through to `ha_discovery_data.*`.

### 8.3 Discovery manager — `ha_discovery_manager.cpp`

- When an entry has an `fw` field, emit:
  - `command_topic` = `…/erd/0x{REQ}/write/{fw}` (the field-write topic).
  - `command_template` = the field-value template.
- Non-RMW entries are emitted exactly as today.

### 8.4 MQTT adapter — `esphome_mqtt_client_adapter.cpp`

- Change the subscription wildcard `…/erd/+/write` → `…/erd/+/write/#`.
- After parsing the ERD (and optional `0x{addr}_` prefix), if four decimal segments follow `write`, parse them as the field geometry and mark the request as a field write.
- Extend `mqtt_client_on_write_request_args_t` (`i_mqtt_client.h`) with the field geometry and an `is_field_write` flag. Full writes leave the flag clear and behave exactly as today.

### 8.5 Write bridge — `erd_write_bridge.cpp` / `.h`

- On a **field write**:
  1. Look up `R` in the generated RMW table. (Not found → treat as a normal write; should not occur.)
  2. Snapshot `base_erd` from `erd_cache` (reuse the `erd_cache_snapshot_*` pattern — copy out of the arena under the lock, never hold the lock across the write).
  3. If the base is missing or its size ≠ the table's `payload_size`: log a warning, publish a failure `write_result`, stay ready. **This is the safe fallback** — the entity no-ops until the base is polled.
  4. Patch the field (byte copy or bit RMW) into a local full-size buffer.
  5. `tiny_gea3_erd_client_write(R, buffer, payload_size)`.
- On a **full write**: unchanged.
- The existing one-write-at-a-time HSM, request-id correlation, and result reporting are reused as-is.

### 8.6 Generated RMW table (new artifact)

```c
typedef struct { tiny_erd_t request_erd; tiny_erd_t base_erd; uint8_t payload_size; } rmw_erd_t;
extern const rmw_erd_t rmw_erds[];
extern const uint16_t rmw_erd_count;
```

Generated by the pipeline (Phase 1: the 16 sub-field request ERDs — `base_erd` = the status ERD). Checked by `scripts/check_generated_sync.sh` / `make check-generated` like the other generated files.

### 8.7 Tests

- **Pipeline:** a matched pair emits `fw` + a field-value `command_template` and an RMW-table row; a divergent/standalone-request ERD stays demoted; the regression test fails if a writable sub-field appears without an RMW-table entry.
- **Write bridge (unit):** byte-aligned patch, bit-aligned patch (set + clear), base-missing → drop + failure result, base-wrong-size → drop, full-write path unchanged, request-id correlation still holds.
- **Adapter (unit):** field topic parses to the correct geometry; full topic still parses; secondary-board field topic parses; malformed segments are dropped.
- **Integration:** end-to-end field write → correct full payload on the wire (mock ERD client), for a bit field and a multi-byte field.

---

## 9. Safety & risks

- **A wrong full-payload write hits a real appliance** (e.g. an incorrect oven temperature or an unintended feature). This is why enablement is gated on a *verified* base and why the default is read-only.
- **Safe fallback:** if the base is not cached (appliance just booted, feature not yet polled, or ERD not supported), the write is dropped with a warning and a failure `write_result` — it never writes a guess.
- **Stale base:** the base is the last polled value. If the appliance changes the field out-of-band between the poll and the write, the RMW can revert that change. This is inherent to RMW and acceptable for user-initiated control; the poll interval bounds the staleness.
- **Consumed requests:** never used as a base (Section 5), so a write can't be built on a `0xFF` reset value.
- **Concurrency:** the base is snapshotted under the cache lock and the patch happens on a local copy; the cache lock is never held across the slow `tiny_gea3_erd_client_write` (existing invariant).

---

## 10. Phasing & acceptance criteria

**Phase 1 (this plan's scope for implementation):** the 18 matched pairs.

- [ ] The 18 matched pairs expose writable sub-fields via field-write topics.
- [ ] A field write reads the status base, patches only the target field, and writes the full payload.
- [ ] Missing/stale base → dropped with a warning + failure `write_result` (no guess written).
- [ ] Full-write path and all previously-writable ERDs are unchanged.
- [ ] Regression test: no writable sub-field without a verified RMW base.
- [ ] `make test`, `make integration-test`, `make check-generated` pass; `esphome compile` of a device yaml passes.

**Phase 2 (future, per-ERD):** the 14 divergent pairs (mask+value / prefix) and the verified standalone-state ERDs, each enabled only after its base-construction is confirmed against the appliance protocol.

**Phase 3 (no action):** the 32 standalone request ERDs + the 2 divergent "none" ERDs remain read-only.
