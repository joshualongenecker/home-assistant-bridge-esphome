# ADR-0013: Every sanitizer and build-variant gate gets its own build directory and its own flags

Status: accepted (2026-11-27)
Related: ADR-0002, ADR-0003, ADR-0006, `docs/guides/development.md`

## Context

The component has correctness properties that the default test build cannot
observe: happens-before edges in the discovery-lifecycle claim protocol (only a
dual-core race detector sees them), the single-protocol build shape (only the
ESPHome codegen emits it), and uninitialized-struct bugs (only visible when
automatic variables are poisoned). Each of those needs different compiler flags,
and `make` will silently reuse objects built with the wrong flags.

## Decision

Each gate is a separate build with an explicit `BUILD_DIR`:

- **ThreadSanitizer** — `make test TSAN=1`, with its own build directory set
  before `OBJS` expands `BUILD_DIR`, because TSan cannot share a binary with
  ASan. It exists to check the happens-before edges of the discovery-lifecycle
  claim protocol (the release store on `phase_` that publishes `trigger_`) the
  way the dual-core device will; the plain ASan build only catches invariant
  violations that happen to manifest. TSan does not run LeakSanitizer, so leak
  detection stays with the ASan variants and the `ASAN_OPTIONS` export is inert
  under `TSAN=1`.
- **Leak detection is on for both suites.** No test owns memory that outlives its
  teardown, so a leak is a real leak — for example a discovery run orphaning its
  ~52 KB of run allocations. `halt_on_error` must stay at its default of 1: with
  `halt_on_error=0` AddressSanitizer prints the LeakSanitizer report but still
  exits 0, so a leak would not fail CI. Debugging with halting disabled is a
  deliberate, explicit option
  (`make test ASAN_OPTIONS="detect_leaks=1 halt_on_error=0"`), not the default.
- **macOS is the exception.** Apple clang's AddressSanitizer runtime has no
  LeakSanitizer, and requesting one aborts the run before a single test
  executes. Leak checking therefore runs on Linux only, which is where CI runs;
  every other ASan and UBSan check still applies on macOS.
- **The integration suite links a separate binary** so the two targets can never
  run each other's stale binary: `make` cannot tell which source set produced a
  shared output path.
- **Protocol-variant gate** — the ESPHome build emits `GEA_BRIDGE_HAS_GEA3` /
  `GEA_BRIDGE_HAS_GEA2` per configured UART, so the component must also compile
  with one protocol's ring buffers absent. The default test build defines
  neither, which `protocol_stack.h` reads as "both stacks", so no other target
  ever compiles the single-protocol shape. `-fsyntax-only` keeps the gate to a
  few seconds and the flags mirror the test build so the same warnings bite;
  `-MMD` is stripped because nothing is linked and it would drop a `.d` file per
  source into the working directory.
- **Pattern-init gate** —
  `make integration-test BUILD_DIR=./build-pattern EXTRA_CXXFLAGS=-ftrivial-auto-var-init=pattern`
  catches embedded structs that own heap but were not value-initialized.
  `BUILD_DIR` must differ from the default, or `make` considers the objects built
  without the flag up to date and the variant tests nothing.

## Consequences

- A gate that shares a build directory with another configuration silently stops
  testing anything. Any new variant needs its own `BUILD_DIR` and, if it changes
  flags, its own object set.
- The defines that compile out a protocol's ring buffers are emitted under
  exactly the conditions that make the `set_gea3_uart` / `set_gea2_uart` calls
  happen. That coupling is load-bearing: `protocol_stack.h` documents why the two
  must agree. The storage saved is 8 447 B for GEA2 and 5 351 B for GEA3.
- A sanitizer run's exit status is only meaningful if the sanitizer is actually
  able to fail the process; when adding a variant, prove it fails on an injected
  defect before trusting a pass.
- Local runs on macOS are not equivalent to CI: leaks are checked only on Linux.

## Code anchors

- `Makefile` — the `TSAN=1` branch and its `BUILD_DIR`, the `ASAN_OPTIONS`
  export and its Darwin guard, `TARGET_INTEGRATION` / `SRCS_INTEGRATION`,
  `check-protocol-variants`, `EXTRA_CXXFLAGS`.
- `components/geappliances_bridge/__init__.py` — the `GEA_BRIDGE_HAS_*` defines
  and the matching `set_gea3_uart` / `set_gea2_uart` calls.
- `protocol_stack.h` — the "both stacks" default and the coupling note.
- `.github/workflows/test.yml` — where each gate runs.
