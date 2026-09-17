# PS1 MAX³ Multi-Cycle Evidence Explorer Design

**Date:** 2026-09-10
**Status:** Approved for implementation
**Base:** `e5d0dbd517d7cc8642ad0169d25ed65b6e1ebc25`

## Goal

Replace the single-frontier local PS1 checkpoint workflow with a bounded, deterministic, multi-cycle diagnostic explorer that can cross multiple unknown BIOS frontiers in one user action, while keeping the normal runtime strict and keeping all speculative behavior explicitly diagnostic.

The explorer must maximize useful commercial boot evidence without committing proprietary game data, without treating speculative results as production semantics, and without requiring the user to run a new executable after every single BIOS/MMIO dependency.

## Current commercial evidence

The latest `jojo-mega-checkpoint-v1` report reached 279439 retired instructions, retained 4096 trace samples, and stopped at BIOS `A0/0x72`. The same report observed one speculative 32-bit write of zero to `0x1F8010F4`.

`0x1F8010F4` is PS1 DICR, the DMA Interrupt Register. PSX-SPX documents it as R/W: bits 0-6 are channel completion interrupt controls, bit 15 is the bus-error flag, bits 16-22 are channel interrupt masks, bit 23 is the master enable, bits 24-30 are channel interrupt flags cleared by writing 1, and bit 31 is the derived master interrupt flag.

Sony Psy-Q documentation defines `_96_remove(void)` as a void function that removes the ISO-9660 filesystem driver. PSX-SPX maps both A0/0x56 and A0/0x72 to `_96_remove()` and notes the original BIOS path is affected by a SysDeqIntRP bug. Because the public ABI is void, JoJo HLE must not synthesize a return value in `$v0`.

## Architecture

### 1. Real DICR state

`Ps1MemoryBus` gains explicit DICR state at physical address `0x1F8010F4`.

- `read32` returns writable control state, pending channel flags, and the derived bit 31.
- `write32` replaces writable control bits and applies write-1-to-clear to channel flags 24-30.
- Reset state is zero.
- No DMA transfer or interrupt source is invented. Channel flags remain zero until a future real DMA implementation raises them.
- The observed commercial `write32(0x1F8010F4, 0)` therefore becomes a real non-speculative access.

### 2. `_96_remove()` HLE

`Ps1BootRuntime` handles A0 selectors `0x56` and `0x72` as the same void `_96_remove()` operation.

- Set an internal logical `bios_iso9660_removed_` state to true.
- Return through `$ra` using the existing BIOS-return helper.
- Preserve `$v0` exactly because the Psy-Q ABI is void.
- Do not alter host-side installed files or remove the prepared `boot.psxexe`; the HLE state models only the guest BIOS driver lifecycle.
- Do not fabricate CD-ROM IRQ queues that do not yet exist.

### 3. Resumable BIOS frontier

The strict `Ps1BootRuntime::run()` behavior remains unchanged for unimplemented BIOS calls: it stops with `bios_call_unimplemented` while the CPU remains at the BIOS table entry.

A diagnostic-only API is added:

```cpp
bool Ps1BootRuntime::apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept;
```

It is valid only while stopped at an A0/B0/C0 BIOS table entry that is not handled by production HLE. It applies one of four explicit diagnostic return policies, then returns through `$ra`:

1. `return_zero` -> `$v0 = 0`
2. `return_one` -> `$v0 = 1`
3. `return_minus_one` -> `$v0 = 0xFFFFFFFF`
4. `preserve_v0` -> leave `$v0` unchanged

No fallback mutates guest memory or invents device side effects. The fallback is never enabled by the normal runtime path.

### 4. Deterministic stagnation watchdog

A diagnostic `stagnation_instruction_limit` is added to `Ps1BootOptions`. Zero disables the watchdog.

During `run()`, the watchdog tracks instructions since the most recent newly observed external dependency in the current segment. A new BIOS table/selector pair or a new speculative MMIO `(address,width,write)` tuple resets the counter. Repeated accesses to the same dependency do not.

When the limit is reached, the segment stops with `diagnostic_stall`. MAX³ uses 2,000,000 instructions. This prevents a speculative branch from spinning forever on a wait loop while preserving `UINT64_MAX` as the per-segment instruction budget.

### 5. Diagnostic state fingerprint

To prune branches that converge to the same guest state, `Ps1BootRuntime` exposes a deterministic 64-bit diagnostic state hash. It covers:

- all R3000A GPRs, HI/LO, PC/next-PC, delayed-load state, delay-slot state, COP0 state, and external interrupt pending state;
- all 2 MiB main RAM bytes and 1 KiB scratchpad bytes;
- known MMIO state (I_STAT, I_MASK, DPCR, DICR, Timer1 mode/counter);
- diagnostic MMIO shadow bytes;
- BIOS HLE state (heap, interrupt hook, ChangeClearPAD, ChangeClearRCnt states, ISO-9660 removal state).

FNV-1a 64-bit is sufficient because this hash is diagnostic deduplication, not a security boundary. The explorer also includes frontier PC/table/selector in the dedup key.

### 6. Snapshot DFS explorer

A new `ps1_max3_explorer` unit owns multi-cycle exploration. It receives a parsed `Ps1Executable`, creates one root `Ps1BootRuntime`, and performs depth-first traversal.

Algorithm:

1. Run strictly until a terminal condition or an unimplemented BIOS frontier.
2. Record a compact cycle summary and merge speculative MMIO/frontier BIOS dependencies into global unique sets.
3. At an unimplemented BIOS frontier, compute the diagnostic state fingerprint.
4. If the same frontier state has already been expanded, mark the node deduplicated and do not branch again.
5. Otherwise clone the stopped `Ps1BootRuntime` four times, apply one of the four diagnostic fallbacks to each clone, and continue each child from that exact snapshot.
6. Stop expanding when depth, node, or total-retired limits are reached, or when the branch reaches another terminal reason such as CPU boundary, diagnostic stall, or a presented commercial frame.

Depth-first traversal is required so the explorer holds only the current path plus child snapshots instead of thousands of simultaneous 2 MiB RAM copies.

### 7. MAX³ bounds

The local MAX³ profile is:

```text
max_nodes                  = 5461
max_branch_depth           = 6
branch_factor              = 4
per_segment_instruction    = UINT64_MAX
stagnation_instruction     = 2000000
max_total_retired          = 1000000000
best_trace_capacity        = 131072
bios_event_capacity        = 65536
mmio_event_capacity        = 65536
```

`5461 = 1 + 4 + 16 + 64 + 256 + 1024 + 4096`, so the node budget can represent a complete four-way decision tree through six speculative BIOS frontier levels when deduplication or the global instruction budget does not terminate exploration earlier.

The one-billion global retired-instruction cap counts work actually executed across all explored segments. It is an explorer safety ceiling, not the per-segment CPU budget; every individual segment still uses `UINT64_MAX`.

### 8. Ranking and preservation

The explorer does not retain heavyweight reports for every node. It retains compact summaries for all nodes and the full report for only the best path segment.

Ranking is deterministic and lexicographic:

1. commercial frames presented;
2. VRAM writes and GPU GP0/GP1 commands;
3. CD-ROM command count;
4. DMA transfer count;
5. number of distinct external dependencies reached along the path;
6. cumulative instructions retired along the path.

Ties prefer the earlier node index.

### 9. Consolidated report

The existing path remains:

`%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`

The new first line is:

`format=jojo-max3-checkpoint-v1`

The report contains:

- explorer configuration and termination reason;
- total nodes executed and total retired instructions actually spent across the search;
- best node index, depth, cumulative instructions and decision path;
- deduplicated BIOS frontier dependencies;
- deduplicated speculative MMIO dependencies;
- one compact summary per explored node;
- the full ordinary `Ps1BootReport` for the best node/segment, delimited by explicit begin/end markers.

No game payload, BIOS ROM data, filenames from proprietary content, or RAM dumps are serialized.

### 10. Runtime/UI isolation

`bootstrap_runtime()` remains strict and never invokes MAX³.

`bootstrap_runtime_local_evidence_to_file()` becomes the MAX³ entry point used by the existing `EXECUTAR CHECKPOINT` button. Its public return type remains `Result<Ps1BootReport>` for source compatibility; it returns the best segment report after saving the consolidated MAX³ file.

The installation manifest and prepared generation remain read-only during exploration.

## Failure handling

- Invalid/missing installation fails before exploration starts.
- Failure to parse the prepared PS-X EXE fails before exploration starts.
- Failure to write the report is returned as an I/O error.
- A CPU boundary is a valid terminal node, not a process failure.
- An unimplemented BIOS call is a branch frontier, not a process failure, unless branch limits prevent further expansion.
- A diagnostic stall is a valid terminal node.
- Hitting the global retired-instruction cap ends further expansion and preserves all evidence collected so far.

## Testing

All production changes use RED -> GREEN TDD with synthetic PS-X EXE fixtures only.

Required contracts:

1. DICR write/read and W1C behavior; diagnostic probe must not report DICR after real implementation.
2. A0/0x72 and A0/0x56 `_96_remove()` return through RA, set logical state, and preserve `$v0`.
3. Diagnostic fallback applies exactly the selected `$v0` behavior and cannot be used away from an unknown BIOS frontier.
4. Stagnation watchdog stops a synthetic loop deterministically.
5. State fingerprint is stable for identical runtime states and differs after RAM/register/MMIO mutation.
6. Explorer branches four ways at one unknown BIOS frontier, resumes from the snapshot, deduplicates converged states, obeys depth/node/global budgets, and ranks the deepest/best path deterministically.
7. MAX³ report format includes consolidated dependencies, cycle summaries, decision paths, and the best report, and excludes PS-X EXE payload bytes.
8. Local-evidence integration leaves the manifest and installed generation unchanged.
9. Linux and Windows x64 CI must both pass; Windows x64 Release artifact is the authority for user delivery.

## Non-goals

- Implementing real CD-ROM sector I/O, GPU, SPU, controller runtime, DMA transfers, timers, or IRQ generation in this change.
- Treating speculative BIOS return values as production HLE semantics.
- Serializing RAM or proprietary game/BIOS content.
- Advancing `main` before commercial evidence warrants it.
