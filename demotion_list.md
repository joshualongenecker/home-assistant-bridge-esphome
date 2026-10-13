# ERDs Demoted to Read-Only (issue #156)

**Rule:** a writable sub-field stays writable **only** if it is the *sole* writable sub-field of the ERD **and** its command writes the *full* payload. Every other writable sub-field is demoted to read-only (`switch`→`binary_sensor`, `number`/`select`→`sensor`).

Because any incorrectly-sized ERD write is rejected by the appliance, a sub-field that writes fewer bytes than the payload can never succeed — it only ever fails. Demoting it to read-only is strictly safer and more honest.

## Summary

| Category | Meaning | ERDs |
|---|---|---|
| A — Clobber | >1 writable sub-field shares a **byte** | 4 |
| B — Multi-writable | >1 writable sub-field, distinct bytes (same payload) | 53 |
| C — Single partial | 1 writable sub-field, writes < full payload | 43 |
| **Total demoted** | | **100** |

ERDs left fully writable (sole sub-field, full-payload write): **467**.

## Category A — Clobber (shares a byte)

Writing one of these sub-fields overwrites the others in the same byte. `0x7051` is a 1-byte ERD so its writes are the correct size and **do land** (real clobber); the other three are multi-byte so their 1-byte writes are rejected as incorrectly-sized (they fail, but are still wrong to expose as switches).

### `0x2f07` — Mabe - Loads Request

- payload 4B, paired `2f08`, request, 3/4 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field shares a byte

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Mabe - Loads Request - Lid lock Request | switch | 3–3 | 0 | 1B | demote → read-only |
| Mabe - Loads Request - Safety Relay Request | switch | 3–3 | 2 | 1B | demote → read-only |
| Mabe - Loads Request - Dispenser Pump Request | switch | 3–3 | 3 | 1B | demote → read-only |

### `0x322e` — Dish Favorite Settings Request

- payload 12B, paired `322f`, request, 5/5 fields writable, [dishwasher.jsonl]
- **Reason:** >1 writable sub-field shares a byte

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Dish Favorite Settings | number | 3–3 | — | 1B | demote → read-only |
| Dish Favorite Settings - validStoredSettings | switch | 0–0 | 0 | 1B | demote → read-only |
| Dish Favorite Settings - steam | switch | 0–0 | 1 | 1B | demote → read-only |
| Dish Favorite Settings - bottleBlast | switch | 0–0 | 2 | 1B | demote → read-only |
| Dish Favorite Settings - silverwareWash | switch | 0–0 | 3 | 1B | demote → read-only |

### `0x7051` — Air Purifier Request

- payload 1B, paired `7053`, request, 5/5 fields writable, [airconditioning.jsonl]
- **Reason:** >1 writable sub-field shares a byte

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Air Purifier - Air Purifier Off | switch | 0–0 | 0 | 1B | demote → read-only |
| Air Purifier - Ultraviolet sterilization | switch | 0–0 | 1 | 1B | demote → read-only |
| Air Purifier - Ozone | switch | 0–0 | 2 | 1B | demote → read-only |
| Air Purifier - Negative ions | switch | 0–0 | 3 | 1B | demote → read-only |
| Air Purifier - PM2.5 | switch | 0–0 | 4 | 1B | demote → read-only |

### `0x9401` — Smoker Cooking Request

- payload 14B, paired `9400`, request, 6/6 fields writable, [smallappliance.jsonl]
- **Reason:** >1 writable sub-field shares a byte

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Smoker Cooking - State Requested | switch | 8–9 | 8 | 1B | demote → read-only |
| Smoker Cooking - Cook Mode Requested | switch | 8–9 | 9 | 1B | demote → read-only |
| Smoker Cooking - Cavity Temperature Requested | switch | 8–9 | 10 | 1B | demote → read-only |
| Smoker Cooking - Cook Time Requested | switch | 8–9 | 11 | 1B | demote → read-only |
| Smoker Cooking - Probe Target Temperature Requested | switch | 8–9 | 12 | 1B | demote → read-only |
| Smoker Cooking - Smoke Level Requested | switch | 8–9 | 13 | 1B | demote → read-only |

## Category B — Multi-writable (distinct bytes, same payload)

More than one writable sub-field in the ERD. Each writes a short payload, so every write is incorrectly-sized and fails; exposing them as controllable is misleading.

### `0x0502` — Still Frame

- payload 2B, 2/2 fields writable, [common.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Still Frame - Still Capture | select | 0–0 | — | 1B | demote → read-only |
| Still Frame - JPEG Resolution | select | 1–1 | — | 1B | demote → read-only |

### `0x100a` — Ice Maker Control

- payload 2B, 2/2 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Ice Maker Control - Freezer Status | select | 0–0 | — | 1B | demote → read-only |
| Ice Maker Control - Fresh Food Status | select | 1–1 | — | 1B | demote → read-only |

### `0x101a` — Temperature Pan Current Setting

- payload 3B, 3/3 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Temperature Pan Current Setting - Temp Pan Mode 1 Setting | select | 0–0 | — | 1B | demote → read-only |
| Temperature Pan Current Setting - Temp Pan Mode 2 Setting | select | 1–1 | — | 1B | demote → read-only |
| Temperature Pan Current Setting - Temp Pan Mode 3 Setting | select | 2–2 | — | 1B | demote → read-only |

### `0x1031` — Key Status

- payload 5B, 3/3 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Key Status - Key Mute | select | 0–0 | — | 1B | demote → read-only |
| Key Status - Key Lock | select | 1–1 | — | 1B | demote → read-only |
| Key Status - Water Dispense Lock | select | 2–2 | — | 1B | demote → read-only |

### `0x1306` — Grow State Request

- payload 12B, paired `1307`, request, 6/6 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Grow State - Grow Requested State | switch | 0–0 | — | 1B | demote → read-only |
| Grow State - Zone Request Section 1 | select | 1–1 | — | 1B | demote → read-only |
| Grow State - Zone Request Section 2 | select | 2–2 | — | 1B | demote → read-only |
| Grow State - Zone Request Section 3 | select | 3–3 | — | 1B | demote → read-only |
| Grow State - Request ID | number | 4–7 | — | 4B | demote → read-only |
| Grow State - Data ID | number | 8–11 | — | 4B | demote → read-only |

### `0x1315` — Paused Mode Request

- payload 8B, paired `1327`, request, 2/2 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Paused Mode - Paused Mode Request | select | 0–0 | — | 1B | demote → read-only |
| Paused Mode - Request ID | number | 4–7 | — | 4B | demote → read-only |

### `0x1321` — Reset Nutrient Cartridge Request

- payload 8B, 2/3 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Reset Nutrient Cartridge Request - Request Type | select | 0–0 | — | 1B | demote → read-only |
| Reset Nutrient Cartridge Request - Nutrient Cartridge Id | select | 1–1 | — | 1B | demote → read-only |

### `0x14c8` — Scanned Barcode Result

- payload 86B, 2/6 fields writable, [refrigeration.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Scanned Barcode Result - Type | select | 16–16 | — | 1B | demote → read-only |
| Scanned Barcode Result - Result | select | 20–20 | — | 1B | demote → read-only |

### `0x2096` — Washer Link 2 Data

- payload 12B, 2/5 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Washer Link 2 Data - Base Cycle Type | select | 4–4 | — | 1B | demote → read-only |
| Washer Link 2 Data - Washer Load Type | select | 5–5 | — | 1B | demote → read-only |

### `0x20c1` — Smart Dispense Adjustability Request

- payload 3B, 3/3 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Smart Dispense Adjustability Request - Substance Type | select | 0–0 | — | 1B | demote → read-only |
| Smart Dispense Adjustability Request - Flow Rate Bucket Selection | select | 1–1 | — | 1B | demote → read-only |
| Smart Dispense Adjustability Request - Dosage Type | select | 2–2 | — | 1B | demote → read-only |

### `0x20d5` — Flex Dispense Request

- payload 2B, 2/2 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Flex Dispense Request - Stage | select | 0–0 | — | 1B | demote → read-only |
| Flex Dispense Request - Method | select | 1–1 | — | 1B | demote → read-only |

### `0x2104` — Washer Remote My Cycle Request

- payload 40B, 2/19 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Washer Remote My Cycle Request - Remote My Cycle Action | select | 4–4 | — | 1B | demote → read-only |
| Washer Remote My Cycle Request - Selected Cycle | select | 5–5 | — | 1B | demote → read-only |

### `0x2143` — Smart Softener Dispense Adjustability Request

- payload 3B, 3/3 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Smart Softener Dispense Adjustability Request - Substance Type | select | 0–0 | — | 1B | demote → read-only |
| Smart Softener Dispense Adjustability Request - Flow Rate Bucket Selection | select | 1–1 | — | 1B | demote → read-only |
| Smart Softener Dispense Adjustability Request - Dosage Type | select | 2–2 | — | 1B | demote → read-only |

### `0x2164` — Cycle Pricing Request

- payload 48B, paired `2165`, request, 32/32 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Cycle Pricing - Cycle n Price[0] | number | 0–1 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[1] | number | 2–3 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[2] | number | 4–5 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[3] | number | 6–7 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[4] | number | 8–9 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[5] | number | 10–11 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[6] | number | 12–13 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[7] | number | 14–15 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[8] | number | 16–17 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[9] | number | 18–19 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[10] | number | 20–21 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[11] | number | 22–23 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[12] | number | 24–25 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[13] | number | 26–27 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[14] | number | 28–29 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n Price[15] | number | 30–31 | — | 2B | demote → read-only |
| Cycle Pricing - Cycle n ID[0] | select | 32–32 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[1] | select | 33–33 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[2] | select | 34–34 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[3] | select | 35–35 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[4] | select | 36–36 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[5] | select | 37–37 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[6] | select | 38–38 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[7] | select | 39–39 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[8] | select | 40–40 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[9] | select | 41–41 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[10] | select | 42–42 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[11] | select | 43–43 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[12] | select | 44–44 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[13] | select | 45–45 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[14] | select | 46–46 | — | 1B | demote → read-only |
| Cycle Pricing - Cycle n ID[15] | select | 47–47 | — | 1B | demote → read-only |

### `0x2166` — Option Pricing Request

- payload 36B, paired `2167`, request, 27/27 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Option Pricing - Option n Price[0] | number | 0–1 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[1] | number | 2–3 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[2] | number | 4–5 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[3] | number | 6–7 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[4] | number | 8–9 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[5] | number | 10–11 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[6] | number | 12–13 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[7] | number | 14–15 | — | 2B | demote → read-only |
| Option Pricing - Option n Price[8] | number | 16–17 | — | 2B | demote → read-only |
| Option Pricing - Option n ID[0] | number | 18–18 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[1] | number | 19–19 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[2] | number | 20–20 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[3] | number | 21–21 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[4] | number | 22–22 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[5] | number | 23–23 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[6] | number | 24–24 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[7] | number | 25–25 | — | 1B | demote → read-only |
| Option Pricing - Option n ID[8] | number | 26–26 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[0] | number | 27–27 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[1] | number | 28–28 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[2] | number | 29–29 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[3] | number | 30–30 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[4] | number | 31–31 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[5] | number | 32–32 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[6] | number | 33–33 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[7] | number | 34–34 | — | 1B | demote → read-only |
| Option Pricing - Option n Level[8] | number | 35–35 | — | 1B | demote → read-only |

### `0x2168` — Mid-Cycle Adjustment Pricing Request

- payload 28B, paired `2169`, request, 18/18 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[0] | number | 0–1 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[1] | number | 2–3 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[2] | number | 4–5 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[3] | number | 6–7 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[4] | number | 8–9 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[5] | number | 10–11 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[6] | number | 12–13 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[7] | number | 14–15 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[8] | number | 16–17 | — | 2B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[0] | number | 18–18 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[1] | number | 19–19 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[2] | number | 20–20 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[3] | number | 21–21 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[4] | number | 22–22 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[5] | number | 23–23 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[6] | number | 24–24 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[7] | number | 25–25 | — | 1B | demote → read-only |
| Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n ID[8] | number | 26–26 | — | 1B | demote → read-only |

### `0x222d` — Flexible Dispensing Tank 1 Configuration Request

- payload 2B, paired `222c`, request, 2/2 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Flexible Dispensing Tank 1 Configuration - Liquid Type | select | 0–0 | — | 1B | demote → read-only |
| Flexible Dispensing Tank 1 Configuration - Liquid Dose | select | 1–1 | — | 1B | demote → read-only |

### `0x222f` — Flexible Dispensing Tank 2 Configuration Request

- payload 2B, paired `222e`, request, 2/2 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Flexible Dispensing Tank 2 Configuration - Liquid Type | select | 0–0 | — | 1B | demote → read-only |
| Flexible Dispensing Tank 2 Configuration - Liquid Dose | select | 1–1 | — | 1B | demote → read-only |

### `0x223c` — Commercial Remote Wash Times Request

- payload 32B, paired `223d`, request, 32/32 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[0] | number | 0–0 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[1] | number | 1–1 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[2] | number | 2–2 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[3] | number | 3–3 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[4] | number | 4–4 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[5] | number | 5–5 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[6] | number | 6–6 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[7] | number | 7–7 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[8] | number | 8–8 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[9] | number | 9–9 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[10] | number | 10–10 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[11] | number | 11–11 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[12] | number | 12–12 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[13] | number | 13–13 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[14] | number | 14–14 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[15] | number | 15–15 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[0] | number | 16–16 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[1] | number | 17–17 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[2] | number | 18–18 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[3] | number | 19–19 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[4] | number | 20–20 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[5] | number | 21–21 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[6] | number | 22–22 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[7] | number | 23–23 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[8] | number | 24–24 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[9] | number | 25–25 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[10] | number | 26–26 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[11] | number | 27–27 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[12] | number | 28–28 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[13] | number | 29–29 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[14] | number | 30–30 | — | 1B | demote → read-only |
| Commercial Remote Wash Times - Cycle n Adjustment Delta Time in Minutes[15] | number | 31–31 | — | 1B | demote → read-only |

### `0x224a` — Commercial Remote Spin Limit Option Request

- payload 16B, paired `224b`, request, 16/16 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[0] | switch | 0–0 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[1] | switch | 1–1 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[2] | switch | 2–2 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[3] | switch | 3–3 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[4] | switch | 4–4 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[5] | switch | 5–5 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[6] | switch | 6–6 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[7] | switch | 7–7 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[8] | switch | 8–8 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[9] | switch | 9–9 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[10] | switch | 10–10 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[11] | switch | 11–11 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[12] | switch | 12–12 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[13] | switch | 13–13 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[14] | switch | 14–14 | — | 1B | demote → read-only |
| Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[15] | switch | 15–15 | — | 1B | demote → read-only |

### `0x224c` — Commercial Remote Dry Times Request

- payload 20B, paired `224d`, request, 20/20 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[0] | number | 0–0 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[1] | number | 1–1 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[2] | number | 2–2 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[3] | number | 3–3 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[4] | number | 4–4 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[5] | number | 5–5 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[6] | number | 6–6 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[7] | number | 7–7 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[8] | number | 8–8 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Base Dry Time in Minutes[9] | number | 9–9 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[0] | number | 10–10 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[1] | number | 11–11 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[2] | number | 12–12 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[3] | number | 13–13 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[4] | number | 14–14 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[5] | number | 15–15 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[6] | number | 16–16 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[7] | number | 17–17 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[8] | number | 18–18 | — | 1B | demote → read-only |
| Commercial Remote Dry Times - Option n Increment Dry Time in Minutes[9] | number | 19–19 | — | 1B | demote → read-only |

### `0x224f` — Commercial Remote Dry Temperature Request

- payload 20B, paired `2250`, request, 10/10 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Dry Temperature - Option n Drying Temperature[0] | number | 0–1 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[1] | number | 2–3 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[2] | number | 4–5 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[3] | number | 6–7 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[4] | number | 8–9 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[5] | number | 10–11 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[6] | number | 12–13 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[7] | number | 14–15 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[8] | number | 16–17 | — | 2B | demote → read-only |
| Commercial Remote Dry Temperature - Option n Drying Temperature[9] | number | 18–19 | — | 2B | demote → read-only |

### `0x2252` — Commercial Remote Cooldown Times Request

- payload 10B, paired `2253`, request, 10/10 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[0] | number | 0–0 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[1] | number | 1–1 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[2] | number | 2–2 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[3] | number | 3–3 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[4] | number | 4–4 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[5] | number | 5–5 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[6] | number | 6–6 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[7] | number | 7–7 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[8] | number | 8–8 | — | 1B | demote → read-only |
| Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[9] | number | 9–9 | — | 1B | demote → read-only |

### `0x2255` — Commercial Remote Cooldown Temperature Request

- payload 20B, paired `2256`, request, 10/10 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[0] | number | 0–1 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[1] | number | 2–3 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[2] | number | 4–5 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[3] | number | 6–7 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[4] | number | 8–9 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[5] | number | 10–11 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[6] | number | 12–13 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[7] | number | 14–15 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[8] | number | 16–17 | — | 2B | demote → read-only |
| Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[9] | number | 18–19 | — | 2B | demote → read-only |

### `0x2316` — Commercial Remote Max Drying Times Request

- payload 10B, paired `2317`, request, 10/10 fields writable, [laundry.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[0] | number | 0–0 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[1] | number | 1–1 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[2] | number | 2–2 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[3] | number | 3–3 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[4] | number | 4–4 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[5] | number | 5–5 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[6] | number | 6–6 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[7] | number | 7–7 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[8] | number | 8–8 | — | 1B | demote → read-only |
| Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[9] | number | 9–9 | — | 1B | demote → read-only |

### `0x5023` — Accent Lighting Request

- payload 12B, paired `5024`, request, 4/4 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Accent Lighting - Custom Color Active | switch | 0–0 | — | 1B | demote → read-only |
| Accent Lighting - Brightness | number | 1–1 | — | 1B | demote → read-only |
| Accent Lighting - Temperature | number | 2–3 | — | 2B | demote → read-only |
| Accent Lighting - Custom Color Code | number | 4–7 | — | 4B | demote → read-only |

### `0x511c` — Upper Oven Cook Time Add Or Subtract Request

- payload 4B, paired `511d`, request, 2/2 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Oven Cook Time Add Or Subtract - Request ID | number | 0–0 | — | 1B | demote → read-only |
| Upper Oven Cook Time Add Or Subtract - Upper Oven Cook Time Add Or Subtract Request | number | 2–3 | — | 2B | demote → read-only |

### `0x5121` — Upper Cavity Cook Recipe Request

- payload 80B, paired `5120`, request, 18/19 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Cavity Cook Recipe - Cook Action | select | 0–0 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Three Hour Cutoff | switch | 1–1 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Allow High Temperature | switch | 2–2 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Door Request State | select | 3–3 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Beep On Start | switch | 4–4 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Seconds Remaining | number | 8–11 | — | 4B | demote → read-only |
| Upper Cavity Cook Recipe - Enabled | switch | 12–12 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Update | switch | 16–16 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Enabled | switch | 20–20 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Clock Type | select | 24–24 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Hours | number | 28–28 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Minutes | number | 29–29 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Seconds | number | 30–30 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Day Part | select | 32–32 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Update | switch | 36–36 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe - Recipe UUID | number | 40–41 | — | 2B | demote → read-only |
| Upper Cavity Cook Recipe - Recipe CRC | number | 42–43 | — | 2B | demote → read-only |
| Upper Cavity Cook Recipe - Request ID | number | 44–44 | — | 1B | demote → read-only |

### `0x5124` — Upper Cavity Cook Recipe Stage 1

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Cavity Cook Recipe Stage 1 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 1 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5125` — Upper Cavity Cook Recipe Stage 2

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Cavity Cook Recipe Stage 2 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 2 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5126` — Upper Cavity Cook Recipe Stage 3

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Cavity Cook Recipe Stage 3 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 3 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5127` — Upper Cavity Cook Recipe Stage 4

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Cavity Cook Recipe Stage 4 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 4 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5128` — Upper Cavity Cook Recipe Stage 5

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Cavity Cook Recipe Stage 5 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Upper Cavity Cook Recipe Stage 5 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x521c` — Lower Oven Cook Time Add Or Subtract Request

- payload 4B, paired `521d`, request, 2/2 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Oven Cook Time Add Or Subtract - Request ID | number | 0–0 | — | 1B | demote → read-only |
| Lower Oven Cook Time Add Or Subtract - Lower Oven Cook Time Add Or Subtract Request | number | 2–3 | — | 2B | demote → read-only |

### `0x5221` — Lower Cavity Cook Recipe Request

- payload 80B, paired `5220`, request, 18/19 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Cavity Cook Recipe - Cook Action | select | 0–0 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Three Hour Cutoff | switch | 1–1 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Allow High Temperature | switch | 2–2 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Door Request State | select | 3–3 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Beep On Start | switch | 4–4 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Seconds Remaining | number | 8–11 | — | 4B | demote → read-only |
| Lower Cavity Cook Recipe - Enabled | switch | 12–12 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Update | switch | 16–16 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Enabled | switch | 20–20 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Clock Type | select | 24–24 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Hours | number | 28–28 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Minutes | number | 29–29 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Seconds | number | 30–30 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Day Part | select | 32–32 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Update | switch | 36–36 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe - Recipe UUID | number | 40–41 | — | 2B | demote → read-only |
| Lower Cavity Cook Recipe - Recipe CRC | number | 42–43 | — | 2B | demote → read-only |
| Lower Cavity Cook Recipe - Request ID | number | 44–44 | — | 1B | demote → read-only |

### `0x5224` — Lower Cavity Cook Recipe Stage 1

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Cavity Cook Recipe Stage 1 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 1 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5225` — Lower Cavity Cook Recipe Stage 2

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Cavity Cook Recipe Stage 2 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 2 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5226` — Lower Cavity Cook Recipe Stage 3

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Cavity Cook Recipe Stage 3 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 3 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5227` — Lower Cavity Cook Recipe Stage 4

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Cavity Cook Recipe Stage 4 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 4 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5228` — Lower Cavity Cook Recipe Stage 5

- payload 150B, 19/50 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Cavity Cook Recipe Stage 5 - Temperature Target Mode | select | 1–1 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Condition | select | 7–7 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Condition | select | 12–12 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Element Control Type | select | 17–17 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 74–74 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 78–78 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 82–82 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 86–86 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 90–90 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 94–94 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 98–98 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 102–102 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 106–106 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Fan State | select | 110–110 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - On Off Request | select | 114–114 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - On Off Request | select | 118–118 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - On Off Request | select | 122–122 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - On Off Request | select | 126–126 | — | 1B | demote → read-only |
| Lower Cavity Cook Recipe Stage 5 - Next Stage | select | 148–148 | — | 1B | demote → read-only |

### `0x5402` — Advantium Cook Request

- payload 19B, paired `5401`, request, 15/15 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Advantium Cook - Remote Cook Request ID | number | 0–0 | — | 1B | demote → read-only |
| Advantium Cook - Cook Action | select | 1–1 | — | 1B | demote → read-only |
| Advantium Cook - Cook Mode | select | 2–2 | — | 1B | demote → read-only |
| Advantium Cook - Target Temperature | number | 3–4 | — | 2B | demote → read-only |
| Advantium Cook - Upper Heater Power Level | number | 5–5 | — | 1B | demote → read-only |
| Advantium Cook - Lower Heater Power Level | number | 6–6 | — | 1B | demote → read-only |
| Advantium Cook - Microwave Power Level | number | 7–7 | — | 1B | demote → read-only |
| Advantium Cook - Convection Power Level | number | 8–8 | — | 1B | demote → read-only |
| Advantium Cook - Cook Time | number | 9–10 | — | 2B | demote → read-only |
| Advantium Cook - Sensor T1 Power Level | number | 11–11 | — | 1B | demote → read-only |
| Advantium Cook - Sensor Dh | number | 12–12 | — | 1B | demote → read-only |
| Advantium Cook - Sensor K | number | 13–14 | — | 2B | demote → read-only |
| Advantium Cook - Sensor T2 Power Level | number | 15–15 | — | 1B | demote → read-only |
| Advantium Cook - Sensor Max T1 | number | 16–17 | — | 2B | demote → read-only |
| Advantium Cook - Warm Option | select | 18–18 | — | 1B | demote → read-only |

### `0x5413` — Primary Oven Remote Precision Cook UI Configuration

- payload 3B, 2/3 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Primary Oven Remote Precision Cook UI Configuration - Precision Cook Probe Required | select | 0–0 | — | 1B | demote → read-only |
| Primary Oven Remote Precision Cook UI Configuration - Precision Cook Preheat Ramp Display Required | select | 1–1 | — | 1B | demote → read-only |

### `0x5416` — Advantium Remote JSON Cook Request

- payload 36B, paired `5417`, request, 29/29 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Advantium Remote JSON Cook - Cook Action | select | 0–0 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Cook Mode | select | 1–1 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Target Temperature | number | 2–3 | — | 2B | demote → read-only |
| Advantium Remote JSON Cook - Upper Heater Power Level | number | 4–4 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Lower Heater Power Level | number | 5–5 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Microwave Power Level | number | 6–6 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Convection Power Level | number | 7–7 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Cook Time | number | 8–9 | — | 2B | demote → read-only |
| Advantium Remote JSON Cook - Sensor T1 Power Level | number | 10–10 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Sensor Dh | number | 11–11 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Sensor K | number | 12–13 | — | 2B | demote → read-only |
| Advantium Remote JSON Cook - Sensor Max T1 | number | 14–15 | — | 2B | demote → read-only |
| Advantium Remote JSON Cook - Sensor T2 Power Level | number | 16–16 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Warm Option | select | 17–17 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Remote Cook Request ID | number | 18–18 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Remote Menu Item Selected | switch | 19–19 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Recipe Index | number | 20–23 | — | 4B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[0] | number | 24–24 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[1] | number | 25–25 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[2] | number | 26–26 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[3] | number | 27–27 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[4] | number | 28–28 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[5] | number | 29–29 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[6] | number | 30–30 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[7] | number | 31–31 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[8] | number | 32–32 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[9] | number | 33–33 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[10] | number | 34–34 | — | 1B | demote → read-only |
| Advantium Remote JSON Cook - Choice Tree Selections[11] | number | 35–35 | — | 1B | demote → read-only |

### `0x5423` — Secondary Oven Remote Precision Cook UI Configuration

- payload 3B, 2/3 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Secondary Oven Remote Precision Cook UI Configuration - Precision Cook Probe Required | select | 0–0 | — | 1B | demote → read-only |
| Secondary Oven Remote Precision Cook UI Configuration - Precision Cook Preheat Ramp Display Required | select | 1–1 | — | 1B | demote → read-only |

### `0x5c11` — Microwave State

- payload 6B, 2/5 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave State - State | select | 0–0 | — | 1B | demote → read-only |
| Microwave State - Cook Mode | select | 1–1 | — | 1B | demote → read-only |

### `0x5c35` — Microwave Steam Cook Mode - Current Setting

- payload 2B, 2/2 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Steam Cook Mode - Current Setting - Steam Cook Current Setting | select | 0–0 | — | 1B | demote → read-only |
| Microwave Steam Cook Mode - Current Setting - Serving Size | select | 1–1 | — | 1B | demote → read-only |

### `0x5c3d` — Staged Microwave Cook Request

- payload 208B, 23/42 fields writable, [range.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Staged Microwave Cook Request - Cook Action | select | 0–0 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - Recipe ID | number | 4–7 | — | 4B | demote → read-only |
| Staged Microwave Cook Request - Cook Time In Seconds | number | 8–9 | — | 2B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 11–11 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 12–12 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 31–31 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 32–32 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 51–51 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 52–52 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 71–71 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 72–72 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 91–91 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 92–92 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 111–111 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 112–112 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 131–131 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 132–132 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 151–151 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 152–152 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 171–171 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 172–172 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Action | select | 191–191 | — | 1B | demote → read-only |
| Staged Microwave Cook Request - End of Cook Message | select | 192–192 | — | 1B | demote → read-only |

### `0x7982` — Motion Sensing Mode Selection Request

- payload 3B, paired `7983`, request, 3/3 fields writable, [airconditioning.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Motion Sensing Mode Selection - Selected Occupancy Standby Timer | number | 0–0 | — | 1B | demote → read-only |
| Motion Sensing Mode Selection - Occupancy Time Remaining (in minutes) to Standby | number | 1–1 | — | 1B | demote → read-only |
| Motion Sensing Mode Selection - Motion Sensing Mode Selection | select | 2–2 | — | 1B | demote → read-only |

### `0x913f` — Light Color Request

- payload 4B, paired `913e`, request, 4/4 fields writable, [smallappliance.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Light Color - Intensity Percentage | number | 0–0 | — | 1B | demote → read-only |
| Light Color - Red | number | 1–1 | — | 1B | demote → read-only |
| Light Color - Green | number | 2–2 | — | 1B | demote → read-only |
| Light Color - Blue | number | 3–3 | — | 1B | demote → read-only |

### `0x9208` — Toaster Oven Requested Cooking Parameters

- payload 12B, 2/7 fields writable, [smallappliance.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |
| Toaster Oven Requested Cooking Parameters - Cook Mode | select | 9–9 | — | 1B | demote → read-only |

### `0x9233` — Toaster Oven Requested Cookie Mode with Preferences Cooking Parameters

- payload 12B, 2/6 fields writable, [smallappliance.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Cookie Mode with Preferences Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |
| Toaster Oven Requested Cookie Mode with Preferences Cooking Parameters - Preferences | select | 9–9 | — | 1B | demote → read-only |

### `0x9235` — Toaster Oven Requested Pizza Mode with Preferences Cooking Parameters

- payload 12B, 2/6 fields writable, [smallappliance.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Pizza Mode with Preferences Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |
| Toaster Oven Requested Pizza Mode with Preferences Cooking Parameters - Preferences | select | 9–9 | — | 1B | demote → read-only |

### `0xd00b` — Temporary Temperature Offset Demand Response Request

- payload 44B, paired `d00c`, request, 16/16 fields writable, [energy.jsonl]
- **Reason:** >1 writable sub-field in the same payload (distinct bytes)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Temporary Temperature Offset Demand Response - Segment Count | number | 0–0 | — | 1B | demote → read-only |
| Temporary Temperature Offset Demand Response - Start Time | number | 4–7 | — | 4B | demote → read-only |
| Temporary Temperature Offset Demand Response - Duration | number | 8–9 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Offset | number | 10–11 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Start Time | number | 12–15 | — | 4B | demote → read-only |
| Temporary Temperature Offset Demand Response - Duration | number | 16–17 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Offset | number | 18–19 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Start Time | number | 20–23 | — | 4B | demote → read-only |
| Temporary Temperature Offset Demand Response - Duration | number | 24–25 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Offset | number | 26–27 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Start Time | number | 28–31 | — | 4B | demote → read-only |
| Temporary Temperature Offset Demand Response - Duration | number | 32–33 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Offset | number | 34–35 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Start Time | number | 36–39 | — | 4B | demote → read-only |
| Temporary Temperature Offset Demand Response - Duration | number | 40–41 | — | 2B | demote → read-only |
| Temporary Temperature Offset Demand Response - Offset | number | 42–43 | — | 2B | demote → read-only |

## Category C — Single partial write

Exactly one writable sub-field, but its command is narrower than the full payload, so the write is incorrectly-sized and always fails. Demoted to a read-only sensor.

### `0x0300` — Alexa Registration Request

- payload 4B, 1/3 fields writable, [common.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 4B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Alexa Registration Request - Request | select | 1–1 | — | 1B | demote → read-only |

### `0x1022` — Desired Adjustable Convertible Drawer Setting

- payload 2B, 1/2 fields writable, [refrigeration.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Desired Adjustable Convertible Drawer Setting - Convertible Drawer Mode | select | 0–0 | — | 1B | demote → read-only |

### `0x131f` — Reset Water Filter Timer Request

- payload 8B, 1/2 fields writable, [refrigeration.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 8B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Reset Water Filter Timer Request - Request Type | select | 0–0 | — | 1B | demote → read-only |

### `0x1320` — Reset Air Filter Timer Request

- payload 8B, 1/2 fields writable, [refrigeration.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 8B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Reset Air Filter Timer Request - Request Type | select | 0–0 | — | 1B | demote → read-only |

### `0x1322` — Clean Cycle Request

- payload 8B, 1/2 fields writable, [refrigeration.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 8B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Clean Cycle Request - Request Type | select | 0–0 | — | 1B | demote → read-only |

### `0x141f` — Barcode Scan History

- payload 20B, 1/4 fields writable, [refrigeration.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 20B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Barcode Scan History - Type | select | 16–16 | — | 1B | demote → read-only |

### `0x2069` — Washer Link Data

- payload 8B, 1/4 fields writable, [laundry.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 8B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Washer Link Data - Base Cycle Type | select | 6–6 | — | 1B | demote → read-only |

### `0x20b8` — Spin Level Option Request

- payload 1B, paired `20b7`, request, 1/1 fields writable, [laundry.jsonl]
- **Reason:** sole writable sub-field writes 2B of a 1B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Spin Level Option | select | 0–0 | — | 2B | demote → read-only |

### `0x5100` — Upper Oven Cook Mode Command

- payload 13B, 1/10 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 13B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Upper Oven Cook Mode Command - Mode | select | 0–0 | — | 1B | demote → read-only |

### `0x5200` — Lower Oven Cook Mode

- payload 13B, 1/10 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 13B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Lower Oven Cook Mode - Mode | select | 0–0 | — | 1B | demote → read-only |

### `0x5c0f` — Microwave Reheat Mode - Current Setting

- payload 1B, 1/1 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 2B of a 1B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Reheat Mode - Current Setting | select | 0–0 | — | 2B | demote → read-only |

### `0x5c10` — Microwave Add 30 Second Command

- payload 2B, 1/2 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Add 30 Second Command - Add 30 Seconds Feature | select | 0–0 | — | 1B | demote → read-only |

### `0x5c1b` — Microwave Cook Timer Modification

- payload 4B, 1/4 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 4B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Cook Timer Modification - Add or Subtract Cook Time | select | 0–0 | — | 1B | demote → read-only |

### `0x5c1e` — Microwave Kitchen Timer Modification

- payload 4B, 1/4 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 4B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Kitchen Timer Modification - Add or Subtract Kitchen Timer Time | select | 0–0 | — | 1B | demote → read-only |

### `0x5c24` — Microwave Auto Defrost Mode - Current Setting

- payload 2B, 1/2 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Auto Defrost Mode - Current Setting - Auto Defrost Mode Current Setting | select | 0–0 | — | 1B | demote → read-only |

### `0x5c28` — Microwave Auto Bake Mode - Current Setting

- payload 2B, 1/2 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Auto Bake Mode - Current Setting - Current Mode | select | 0–0 | — | 1B | demote → read-only |

### `0x5c2a` — Microwave Auto Roast Mode - Current Setting

- payload 2B, 1/2 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 2B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Auto Roast Mode - Current Setting - Current Mode | select | 0–0 | — | 2B | demote → read-only |

### `0x5c2e` — Microwave Protein Mode - Current Setting

- payload 2B, 1/2 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Protein Mode - Current Setting - Protein Type current Setting | select | 0–0 | — | 1B | demote → read-only |

### `0x5c33` — Microwave Melt Mode - Current Setting

- payload 2B, 1/2 fields writable, [range.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 2B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Microwave Melt Mode - Current Setting - Melt Type Current Setting | select | 0–0 | — | 1B | demote → read-only |

### `0x79c1` — Indoor Temperature Chassis/Install Adjustment Request

- payload 4B, paired `79c0`, request, 1/1 fields writable, [airconditioning.jsonl]
- **Reason:** sole writable sub-field writes 4B of a 4B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Indoor Temperature Chassis/Install Adjustment | number | 0–1 | — | 4B | demote → read-only |

### `0x900b` — Coffee Brewer Start Brew

- payload 3B, 1/3 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 3B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Coffee Brewer Start Brew - Strength | select | 0–0 | — | 1B | demote → read-only |

### `0x901e` — Espresso Maker Requested Brew Parameters

- payload 6B, 1/4 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 6B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Espresso Maker Requested Brew Parameters - Brew Type | select | 2–2 | — | 1B | demote → read-only |

### `0x904b` — Espresso Maker Requested MyCup/MyBrew Parameters

- payload 6B, 1/4 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 6B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Espresso Maker Requested MyCup/MyBrew Parameters - Brew Type | select | 2–2 | — | 1B | demote → read-only |

### `0x9052` — Coffee Brewer Request Brew Settings

- payload 14B, 1/11 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 14B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Coffee Brewer Request Brew Settings - Action | select | 13–13 | — | 1B | demote → read-only |

### `0x920b` — Toaster Oven Requested Air Fry Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Air Fry Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x920d` — Toaster Oven Requested Bake Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Bake Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x920f` — Toaster Oven Requested Broil Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Broil Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9211` — Toaster Oven Requested Roast Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Roast Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9213` — Toaster Oven Requested Reheat Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Reheat Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9215` — Toaster Oven Requested Warm Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Warm Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9217` — Toaster Oven Requested Slow Cook Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Slow Cook Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9219` — Toaster Oven Requested Dehydrate Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Dehydrate Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x921b` — Toaster Oven Requested Proof Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Proof Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x921d` — Toaster Oven Requested Cookie Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Cookie Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x921f` — Toaster Oven Requested Pizza Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Pizza Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9221` — Toaster Oven Requested Bagel Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Bagel Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9223` — Toaster Oven Requested Toast Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Toast Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9227` — Toaster Oven Requested Crisp Finish Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Crisp Finish Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x9231` — Toaster Oven Requested Cake Mode Cooking Parameters

- payload 12B, 1/5 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Toaster Oven Requested Cake Mode Cooking Parameters - Size | select | 1–1 | — | 1B | demote → read-only |

### `0x930a` — Cycle Timer Absolute Time Modification

- payload 8B, 1/4 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 8B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Cycle Timer Absolute Time Modification - Add or Subtract Cycle Time | select | 0–0 | — | 1B | demote → read-only |

### `0x9602` — Timer ERD for Smart Plug

- payload 8B, 1/2 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 8B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Timer ERD for Smart Plug - Action on Expiry | select | 0–0 | — | 1B | demote → read-only |

### `0x9606` — Over/Under Voltage Notification ERD for Smart Plug

- payload 12B, 1/3 fields writable, [smallappliance.jsonl]
- **Reason:** sole writable sub-field writes 1B of a 12B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Over/Under Voltage Notification ERD for Smart Plug - Voltage Type | select | 0–0 | — | 1B | demote → read-only |

### `0xd023` — Electrical Pricing Structure Selection Request

- payload 4B, paired `d022`, request, 1/1 fields writable, [energy.jsonl]
- **Reason:** sole writable sub-field writes 2B of a 4B payload (write is incorrectly sized -> fails)

| Sub-field | Domain | Byte(s) | Bit | Cmd | Action |
|---|---|---|---|---|---|
| Electrical Pricing Structure Selection | select | 0–1 | — | 2B | demote → read-only |

