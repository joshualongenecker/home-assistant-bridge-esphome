# GE Appliances Bridge Constants — Specification

## 1. Overview

### 1.1 Purpose

Defines well-known ERD identifiers, protocol constants, and inline helper functions used across the bridge implementation files. All constants are `static constexpr` and all helpers are `static inline`, so each translation unit gets its own copy without ODR violations.

### 1.2 Responsibilities

- Declare well-known GEA ERD identifiers for device identity and feature bit parsing.
- Define the GEA bus broadcast address and autodiscovery startup delay.
- Provide `is_feature_bit_erd()` to test whether an ERD is one of the appliance API feature bit ERDs.
- Provide `read_be64()` to read big-endian byte buffers as 64-bit integers.

### 1.3 Not Responsible For

- ERD value interpretation or parsing logic (owned by the feature bit parser and ERD cache).
- Protocol-level message construction (owned by the GEA protocol stack).
- Runtime configuration or dynamic ERD discovery (owned by the startup sequence).

---

## 2. Interface

### 2.1 Device Identity ERDs

Used for device ID generation during the startup sequence.

| Constant | Value | Description |
|----------|-------|-------------|
| `ERD_MODEL_NUMBER` | `0x0001` | Appliance model number. |
| `ERD_SERIAL_NUMBER` | `0x0002` | Appliance serial number. |
| `ERD_APPLIANCE_TYPE` | `0x0008` | Appliance type identifier. |

### 2.2 Appliance API Feature Bit ERDs

ERD `0x0092` reports common-feature flags; ERDs `0x0093`–`0x0097` and `0x0109`–`0x010D` each report one appliance-specific API group. Each has the layout `[2B type][2B version][4B bitmap]` (8 bytes total).

| Constant | Value | Description |
|----------|-------|-------------|
| `ERD_COMMON_FEATURE_API` | `0x0092` | Common feature flags shared across all appliances. |
| `ERD_APPLIANCE_FEATURE_API_0` | `0x0093` | Appliance-specific feature group 0. |
| `ERD_APPLIANCE_FEATURE_API_1` | `0x0094` | Appliance-specific feature group 1. |
| `ERD_APPLIANCE_FEATURE_API_2` | `0x0095` | Appliance-specific feature group 2. |
| `ERD_APPLIANCE_FEATURE_API_3` | `0x0096` | Appliance-specific feature group 3. |
| `ERD_APPLIANCE_FEATURE_API_4` | `0x0097` | Appliance-specific feature group 4. |
| `ERD_APPLIANCE_FEATURE_API_5` | `0x0109` | Appliance-specific feature group 5. |
| `ERD_APPLIANCE_FEATURE_API_6` | `0x010A` | Appliance-specific feature group 6. |
| `ERD_APPLIANCE_FEATURE_API_7` | `0x010B` | Appliance-specific feature group 7. |
| `ERD_APPLIANCE_FEATURE_API_8` | `0x010C` | Appliance-specific feature group 8. |
| `ERD_APPLIANCE_FEATURE_API_9` | `0x010D` | Appliance-specific feature group 9. |

### 2.3 Protocol Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `GEA_BROADCAST_ADDRESS` | `0xFF` | GEA bus broadcast address; all nodes respond. Used during autodiscovery. |
| `AUTODISCOVERY_STARTUP_DELAY_MS` | `10000` | Default delay (ms) before the bridge starts MQTT and appliance discovery after initialization. |
| `APPLIANCE_FEATURE_ERD_SIZE` | `8` | Byte size of each appliance feature API ERD value (`[2B type][2B version][4B bitmap]`). |

### 2.4 Inline Helper Functions

#### `is_feature_bit_erd(erd)`

```c
static inline bool is_feature_bit_erd(tiny_erd_t erd);
```

Returns `true` when `erd` is one of the 11 appliance API feature bit ERDs (`ERD_COMMON_FEATURE_API` or `ERD_APPLIANCE_FEATURE_API_0` through `ERD_APPLIANCE_FEATURE_API_9`).

**Usage:** Used by the feature bit parser to filter incoming ERD responses and determine which values contain feature bitmaps.

#### `read_be64(buf, size)`

```c
static inline uint64_t read_be64(const uint8_t* buf, uint8_t size);
```

Reads up to 8 bytes from a big-endian byte buffer as a 64-bit integer. The GEA protocol transmits ERD values MSB-first (big-endian). If `size` exceeds 8, only the first 8 bytes are read.

**Usage:** Used to decode feature bit bitmaps and other multi-byte ERD values from the GEA protocol response buffer.

---

## 3. Behavior

### 3.1 Feature ERD Identification

#### Requirement 3.1.1: Complete Coverage

`is_feature_bit_erd` MUST return `true` for all 11 feature bit ERDs and `false` for all other ERD identifiers.

**Rationale:** The feature bit parser relies on this function to distinguish feature bit responses from other ERD data. A false positive would cause the parser to misinterpret non-feature data as a feature bitmap; a false negative would cause a feature group to be silently skipped.

**Implementation:** `components/geappliances_bridge/geappliances_bridge_constants.h` lines 59–72.

**Verification:** Assert `is_feature_bit_erd(0x0092)` through `is_feature_bit_erd(0x010D)` (skipping non-feature IDs) return `true`; assert `is_feature_bit_erd(0x0001)` returns `false`.

### 3.2 Big-Endian Reading

#### Requirement 3.2.1: Size Capping

`read_be64` MUST cap the read to 8 bytes maximum, even if `size` is larger.

**Rationale:** The return type is `uint64_t` (8 bytes). Reading more would overflow the accumulator. The cap prevents undefined behavior from oversized buffers.

**Implementation:** `components/geappliances_bridge/geappliances_bridge_constants.h` line 79.

#### Requirement 3.2.2: Big-Endian Interpretation

`read_be64` MUST interpret the input buffer as big-endian (MSB-first), shifting the accumulator left by 8 bits for each byte.

**Rationale:** The GEA protocol transmits multi-byte values in big-endian order. This function provides a portable way to decode them regardless of the host architecture's native endianness.

**Implementation:** `components/geappliances_bridge/geappliances_bridge_constants.h` lines 76–84.

---

## 4. Notes

1. **`static constexpr` for all constants.** Each translation unit gets its own copy, avoiding ODR violations and eliminating the need for a separate `.cpp` definition file.

2. **Feature ERD gap.** The appliance feature API ERDs are not contiguous: they span `0x0093`–`0x0097` and `0x0109`–`0x010D`. The gap between `0x0097` and `0x0109` contains other ERDs used by the appliance. `is_feature_bit_erd` checks each ID individually rather than using a range check.

3. **Dependency on `tiny_gea3_erd_client.h`.** The `tiny_erd_t` type is imported from the GEA protocol stack via `extern "C"` inclusion of `tiny_gea3_erd_client.h`.
