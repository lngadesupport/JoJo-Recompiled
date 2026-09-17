# PS1 HLE BIOS Modularization Design

**Date:** 2026-09-11  
**Status:** Design approved in chat; awaiting written-spec review  
**Branch:** `feature/ps1-max3-multicycle-v0`  
**Base:** `0fa7a3c52d16c5c9a26e381fab667a744e5c1983`

## 1. Goal

Extract PlayStation 1 BIOS high-level emulation semantics from `Ps1BootRuntime` into a focused `Ps1HleBios` component without changing observed runtime behavior.

The resulting architecture keeps `Ps1BootRuntime` responsible for CPU execution, BIOS-vector detection, diagnostics, MMIO boundaries, boot reporting, and diagnostic fallback policy, while `Ps1HleBios` owns the production semantics and persistent logical state of the BIOS services already implemented for JoJo.

This change does not add generic PlayStation BIOS compatibility. It creates the boundary required to extend JoJo-specific HLE safely as new commercial execution evidence identifies additional BIOS dependencies.

## 2. Binding Constraints

This design inherits the active PS1 architecture constraints and makes them explicit for this subsystem:

1. No proprietary PlayStation BIOS image is required, embedded, downloaded, committed, or distributed.
2. HLE is JoJo-specific and implements only services demonstrated necessary by the supported startup/runtime path.
3. Unknown BIOS calls remain explicit unsupported frontiers; they must never be silently treated as successful production calls.
4. `Ps1BootRuntime::run()` remains strict for unimplemented BIOS calls and continues to return `Ps1BootStopReason::bios_call_unimplemented`.
5. Diagnostic MAX³ fallbacks remain diagnostic-only and are not moved into production HLE semantics.
6. Existing commercial game payload, RAM contents, BIOS ROM contents, or proprietary data must not be serialized by this subsystem.
7. Existing MAX³ diagnostic-state deduplication must remain deterministic after the refactor.
8. Linux and Windows x64 test/build behavior must remain source-compatible unless a public interface is intentionally changed by this design.
9. Production changes use RED -> GREEN TDD with synthetic fixtures only.

## 3. Current Problem

`Ps1BootRuntime` currently performs four distinct responsibilities in one unit:

- executes the R3000A reference core;
- recognizes A0/B0/C0 BIOS entry vectors and records diagnostics;
- implements individual BIOS services;
- stores BIOS-specific logical state.

The production BIOS dispatcher is currently an internal `handle_bios_call(...)` function taking multiple references to runtime-owned state. Each new HLE call therefore expands `Ps1BootRuntime`'s private state and dispatcher signature.

That structure was acceptable while only a few BIOS calls existed, but MAX³ is explicitly designed to discover more frontiers. Continuing to add selectors in `ps1_boot_runtime.cpp` would make CPU orchestration, evidence collection, and BIOS semantics increasingly coupled.

## 4. Recommended Architecture

Introduce a dedicated component:

```text
R3000A execution
      |
      v
Ps1BootRuntime
  - detects A0/B0/C0 vector
  - records Ps1BiosCallSummary
  - asks Ps1HleBios to dispatch
      |
      +---- handled ------> continue execution
      |
      +---- unimplemented -> strict BIOS frontier / MAX³ branch point
```

Files:

```text
src/core/ps1_hle_bios.h
src/core/ps1_hle_bios.cpp
```

`Ps1HleBios` owns only BIOS HLE semantics and BIOS HLE state. It does not own the memory bus, instruction loop, `Ps1BootReport`, MAX³ traversal, or runtime stop policy.

## 5. Public Interface

The subsystem exposes a narrow production interface.

```cpp
enum class Ps1HleBiosDispatchStatus : std::uint8_t {
    handled,
    unimplemented,
};

struct Ps1BiosHeapState {
    std::uint32_t base{};
    std::uint32_t size{};
};

class Ps1HleBios {
public:
    [[nodiscard]] Ps1HleBiosDispatchStatus dispatch(
        R3000aState& cpu,
        std::uint32_t table_physical,
        std::uint32_t selector) noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(
        std::uint32_t counter) const noexcept;
    [[nodiscard]] bool iso9660_removed() const noexcept;
};
```

`Ps1BiosHeapState` moves from `ps1_boot_runtime.h` into `ps1_hle_bios.h` because it describes HLE-owned state rather than runtime orchestration state.

The dispatch result is intentionally binary. Production HLE either handled the exact table/selector pair or it did not. There is no generic success fallback in this interface.

## 6. BIOS Return Semantics

The common BIOS return operation moves into `Ps1HleBios` as a private helper.

For a handled BIOS call it performs the same state transition as the existing implementation:

```text
pc       = $ra
next_pc  = pc + 4
delay_slot cleared
$zero    = 0
```

A selector-specific handler may update `$v0` only where the observed/public ABI requires a return value.

Void calls preserve `$v0`.

Unimplemented calls do not modify CPU state and do not return through `$ra`.

## 7. Initial Supported Services

The modular component preserves exactly the production HLE surface already implemented at the base commit.

### A0/0x39 — InitHeap

Inputs:

- `$a0`: heap base
- `$a1`: heap size

Effects:

- store `Ps1BiosHeapState{a0, a1}`;
- return through `$ra`;
- do not invent heap allocation behavior beyond the existing logical state contract.

### A0/0x56 and A0/0x72 — `_96_remove` / RemoveISO9660 aliases

Effects:

- set logical ISO9660 removal state to true;
- preserve `$v0` because the call is void;
- return through `$ra`;
- do not remove host files or mutate the prepared installation.

Both selectors share one semantic handler.

### B0/0x19 — HookEntryInt

Inputs:

- `$a0`: interrupt hook address

Effects:

- store the hook address;
- return through `$ra`.

This design does not add interrupt dispatch behavior beyond the state already modeled.

### B0/0x5B — ChangeClearPad

Inputs:

- `$a0`: enabled/nonzero flag

Effects:

- store pad/card auto-ack state as boolean;
- return through `$ra`.

### C0/0x0A — ChangeClearRCnt

Inputs:

- `$a0`: root-counter index
- `$a1`: enabled/nonzero flag

For indices `0..3`:

- read the previous logical state, treating an unset state as false;
- write the new logical state;
- set `$v0` to `1` if the previous state was true, otherwise `0`;
- return through `$ra`.

For an out-of-range counter index, the call is not handled by this implementation. CPU and HLE state remain unchanged and the runtime exposes it as an unimplemented BIOS frontier.

## 8. Unknown Call Policy

`Ps1HleBios::dispatch()` returns `unimplemented` when any of the following is true:

- the physical table is not a supported BIOS table entry;
- the table is recognized but the selector has no implemented JoJo HLE semantic;
- the selector is known but its input is outside the currently modeled semantic domain, such as `ChangeClearRCnt` with a counter index above 3.

On `unimplemented`:

- no GPR changes are made;
- PC/next-PC and delay-slot state are unchanged;
- no HLE state is mutated;
- no memory or MMIO side effects occur.

The caller owns the diagnostic. In `Ps1BootRuntime`, this continues to mean `bios_call_unimplemented` with the existing `Ps1BiosCallSummary` containing guest PC, physical table, selector, `$a0-$a3`, and `$ra`.

## 9. Runtime Integration

`Ps1BootRuntime` replaces its individual BIOS state members with one value:

```cpp
Ps1HleBios bios_{};
```

The BIOS-vector path remains in `Ps1BootRuntime::run()` because it is part of runtime orchestration and report generation.

The runtime sequence is:

1. convert guest PC to physical address;
2. determine whether it is A0, B0, or C0;
3. update BIOS-dependency progress tracking;
4. increment `bios_call_count`;
5. append `Ps1BiosCallSummary` to the bounded report buffer;
6. call `bios_.dispatch(cpu_, table, cpu_.gpr[9])`;
7. on `handled`, clear `diagnostic_bios_frontier_pending_` and continue;
8. on `unimplemented`, set `diagnostic_bios_frontier_pending_`, set stop reason to `bios_call_unimplemented`, and return the report.

No reporting responsibility moves into `Ps1HleBios`.

## 10. Diagnostic Fallback Isolation

`Ps1BootRuntime::apply_diagnostic_bios_fallback()` remains owned by `Ps1BootRuntime`.

Reason: fallback policies are MAX³ exploration mechanisms, not BIOS semantics.

The fallback continues to:

- require `diagnostic_bios_frontier_pending_`;
- verify the CPU is still at an A0/B0/C0 BIOS vector;
- optionally synthesize only the selected `$v0` diagnostic return policy;
- return through `$ra`;
- clear the pending diagnostic frontier.

The helper used to return from BIOS may be duplicated as a tiny private runtime helper or shared through a narrowly scoped non-public helper. The production `Ps1HleBios` API must not expose a generic diagnostic-return operation.

## 11. Deterministic State Hash

`Ps1HleBios` owns hashing of its own logical state through:

```cpp
std::uint64_t Ps1HleBios::diagnostic_state_hash() const noexcept;
```

The hash covers, in stable order:

1. heap-state presence;
2. heap base and size when present;
3. interrupt-hook presence and value;
4. pad/card auto-ack presence and value;
5. presence/value for all four root-counter auto-ack slots in index order;
6. ISO9660 removal state.

The exact deterministic primitive remains FNV-1a 64-bit, matching the diagnostic-only purpose already used by MAX³.

`Ps1BootRuntime::diagnostic_state_hash()` continues to hash CPU state, RAM/scratchpad/MMIO state, and `diagnostic_bios_frontier_pending_`, but incorporates `bios_.diagnostic_state_hash()` instead of directly hashing every BIOS field.

The migration must preserve the invariant that two logically identical complete runtime states produce identical hashes and that a meaningful HLE-state change changes the complete runtime hash.

Bit-for-bit equality with the numeric hash emitted by the pre-refactor implementation is not a required compatibility contract because the hash is not persisted as a production ABI or security identifier. Determinism and state discrimination are the requirements.

## 12. Accessors and Source Compatibility

Existing `Ps1BootRuntime` inspection methods remain available so current tests and diagnostic code do not need to know the internal ownership change:

```cpp
bios_heap_state()
bios_interrupt_hook_address()
bios_pad_card_auto_ack_enabled()
bios_root_counter_auto_ack_enabled(counter)
bios_iso9660_removed()
```

Each becomes a forwarding accessor to `bios_`.

This keeps the refactor narrow while allowing focused new tests to exercise `Ps1HleBios` directly.

## 13. Error and Exception Behavior

The HLE dispatch path is `noexcept` and does not allocate as part of dispatch.

There is no `Result<T>` error for an unsupported selector. Unsupported BIOS semantics are an expected runtime frontier represented by `Ps1HleBiosDispatchStatus::unimplemented` and converted by `Ps1BootRuntime` into the existing boot stop reason.

No host exception is used to model guest BIOS behavior.

## 14. Testing Strategy

All implementation follows RED -> GREEN TDD.

### Focused `Ps1HleBios` tests

A new `tests/test_ps1_hle_bios.cpp` proves:

1. A0/0x39 captures heap base/size and returns through `$ra`.
2. A0/0x56 and A0/0x72 set ISO9660-removed state, preserve `$v0`, and return through `$ra`.
3. B0/0x19 captures the interrupt hook and returns through `$ra`.
4. B0/0x5B stores false for zero and true for nonzero.
5. C0/0x0A returns the previous state in `$v0`, stores the new state, and supports indices 0..3.
6. C0/0x0A with an out-of-range index returns `unimplemented` and changes neither CPU nor HLE state.
7. Unknown selectors return `unimplemented` without changing CPU or HLE state.
8. Unknown table values return `unimplemented` without changing CPU or HLE state.
9. `$zero` is restored to zero on every handled return.
10. HLE diagnostic hash is stable for identical state and changes after each represented logical-state mutation.

### Runtime regression tests

Existing runtime tests continue proving:

- report-level BIOS event recording remains unchanged;
- strict unknown-BIOS stop behavior remains unchanged;
- diagnostic fallback still works only at a pending unknown frontier;
- existing accessors expose the same logical state;
- `_96_remove`, ChangeClearPad, ChangeClearRCnt, InitHeap, and HookEntryInt behavior remains intact through the runtime path;
- MAX³ can clone runtime snapshots and hash states deterministically after the ownership change.

### Full regression gate

Before the implementation is considered complete:

- focused HLE tests pass;
- all existing PS1/R3000A/MAX³ tests pass;
- full CTest suite passes on the available development environment;
- GitHub Actions Linux and Windows x64 jobs pass when CI is run for the branch.

Commercial image evidence is not required to prove this structural refactor because the intended guest-visible semantics are unchanged. Commercial evidence remains authoritative for deciding which new BIOS calls to implement next.

## 15. Build Integration

`CMakeLists.txt` adds:

```text
src/core/ps1_hle_bios.cpp
```

to the core target and:

```text
tests/test_ps1_hle_bios.cpp
```

to the test target.

No external dependency is introduced.

## 16. Migration Sequence

Implementation proceeds in independently verifiable stages:

1. add failing focused `Ps1HleBios` tests for the existing BIOS contracts;
2. add `ps1_hle_bios.h/.cpp` with the minimal implementation required for those tests;
3. add deterministic HLE-state hashing tests and implementation;
4. replace `Ps1BootRuntime`'s BIOS fields/dispatcher with `Ps1HleBios` while retaining forwarding accessors;
5. prove runtime unknown-frontier and diagnostic-fallback behavior is unchanged;
6. prove MAX³ state hashing/exploration regression tests remain green;
7. run the full suite and CI gates.

Each stage should be committed separately when it forms an independently reviewable unit.

## 17. Non-Goals

This change does not:

- implement any newly discovered BIOS selector;
- emulate a complete Sony PlayStation BIOS;
- add BIOS ROM loading;
- change the R3000A executor;
- add GPU, SPU, CD-ROM, DMA, timer, SIO, GTE, or interrupt-controller behavior;
- alter MAX³ branch policies or search limits;
- reinterpret speculative MAX³ fallback values as production semantics;
- change the installation format or manifest;
- change commercial readiness claims;
- serialize BIOS/game/RAM data;
- promote the branch to `main`.

## 18. Completion Criteria

This design is complete when all of the following are true:

1. `Ps1HleBios` is the sole owner of production BIOS HLE state and selector semantics.
2. `Ps1BootRuntime` still owns vector detection, reporting, strict frontier policy, and diagnostic fallback policy.
3. Every previously supported BIOS call has equivalent tested behavior.
4. Unsupported calls remain non-mutating explicit frontiers.
5. Existing `Ps1BootRuntime` state inspection methods remain source-compatible through forwarding.
6. MAX³ runtime-state hashing remains deterministic and sensitive to HLE state.
7. No proprietary BIOS or game content is introduced.
8. Full regression and CI gates are green.
