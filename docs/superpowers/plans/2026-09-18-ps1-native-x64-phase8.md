# PS1 Native x64 Backend — Phase 8

Date: 2026-09-18
Branch: `feature/ps1-native-x64-phase8-prep`
Base: Phase 7 validation infrastructure branch
Guest ISA: Sony PlayStation R3000A / MIPS-I
Host authority: Windows x64

## Goal

Build a title-scoped R3000A-to-x64 execution path while preserving the existing R3000A reference executor as the semantic oracle.

Phase 8 must not reuse the historical Dreamcast/SH-4 backend plans. Those documents are retained only as project history.

## Current Phase 8 foundation

The Phase 8 preparation branch introduces:

- explicit R3000A block IR;
- correct branch/jump target calculation;
- mandatory MIPS delay-slot inclusion;
- reachable CFG discovery rather than linear decoding of arbitrary payload bytes;
- bounded CFG/block budgets;
- conservative native-lowering classification;
- a native ALU semantic kernel differential-tested against `step_r3000a()`.

## Promotion rule

A block is initially native-eligible only when all of the following are true:

- the block terminator is simple fallthrough;
- no delay-slot execution is active;
- every instruction is a non-trapping ALU operation already covered by differential tests;
- no guest memory/MMIO access is required;
- no COP0/GTE operation is required;
- no exception or interrupt needs to be taken.

Everything else remains on the reference executor until its exact semantics are promoted and tested.

## Required progression

1. R3000A instruction IR and delay-slot-safe blocks.
2. Reachable CFG with bounded discovery.
3. Conservative native lowering plan.
4. Portable semantic kernel with differential tests.
5. Windows x64 executable-memory abstraction.
6. Minimal x64 emitter for the already-proven ALU subset.
7. Differential native-vs-reference block execution on Windows.
8. Native block cache keyed by exact code bytes/entry/ABI.
9. Safe dispatcher with reference fallback.
10. Gradual promotion of control flow, memory, HI/LO, COP0 and GTE only after differential coverage.
11. Integration into the continuous PS1 runtime.
12. Commercial performance validation only after Phase 7 gameplay evidence exists.

## Correctness invariants

- `$zero` is always zero.
- Load-delay semantics must match the reference executor.
- Branch and jump delay slots execute exactly once.
- Native execution must not suppress interrupts/exceptions.
- Self-modifying or changed code invalidates the matching native cache entry.
- Unsupported semantics never silently execute as approximations.
- Guest-visible state hashes must not include observation-only telemetry.
- A failed native eligibility/check falls back before mutating guest state.
- Windows x64 is authoritative for executable-code ABI behavior.

## Commercial boundary

Phase 8 preparation can proceed while Phase 7 waits for the user's missing JoJo data track, but Phase 8 cannot be called release-ready until the current commercial gameplay path is validated and the native dispatcher is proven equivalent on that path.

No commercial game bytes, proprietary BIOS bytes or generated native code from commercial payloads are committed to Git or CI artifacts.
