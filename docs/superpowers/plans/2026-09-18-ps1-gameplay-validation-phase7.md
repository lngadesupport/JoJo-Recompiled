# PS1 Gameplay Validation — Phase 7

Date: 2026-09-18
Branch: `feature/ps1-gameplay-validation-phase7`
Base Phase 6 head: `a7039ef62bc39b4622f4e8ced54a6298acccc7fd`
Phase 6 Final Gate: GitHub Actions run `35312132685`

## Goal

Advance the supported user-supplied JoJo PS1 image from synthetic runtime coverage to current commercial gameplay evidence without guessing missing hardware behavior.

Phase 7 is evidence-driven. A synthetic test can prove a component contract, but only a fresh run of the supported commercial image can prove title/menu/audio/input/save/fight behavior.

## Runtime evidence already implemented

The continuous Windows runtime now preserves a bounded, payload-free session report even when no unsupported frontier is reached.

It records:

- session termination type: bounded run, frontier stop, manual stop, or periodic checkpoint;
- total retired instructions and execution segments;
- completed logical frames;
- first visible non-black frame evidence;
- number of non-black frames observed;
- frame-hash change count for dynamic-video evidence;
- controller poll counts for both ports;
- polls where at least one active-low button was pressed;
- Memory Card sector reads and writes for both ports;
- total SPU sample frames and non-zero PCM sample count;
- cumulative DMA transfer count;
- cumulative CD-ROM command count;
- cumulative GPU GP0 word count and GP1 command count;
- cumulative VRAM writes;
- cumulative VBlank count;
- cumulative bounded recent CD-ROM command history;
- current frontier and last-slice CPU/MMIO/BIOS/GPU evidence.

The app atomically refreshes `commercial-session.txt` every 60 completed frames and on manual stop. Unsupported execution still writes `commercial-frontier.txt`.

## Objective validation signals

The report exposes independent booleans rather than one broad playability claim:

1. visible frame observed;
2. dynamic video observed through changing frame hashes;
3. controller polling observed;
4. a pressed controller state consumed by SIO0;
5. non-silent SPU PCM observed;
6. Memory Card read observed;
7. Memory Card write observed.

These signals do not by themselves prove a full fight or complete game compatibility.

## Phase 7 execution loop

For every fresh commercial run:

1. launch the exact supported legal image through the direct-source runtime;
2. preserve periodic session evidence;
3. if an explicit frontier stops execution, implement only that observed dependency;
4. add a synthetic regression contract for the dependency;
5. keep unsupported behavior explicit until its real semantics are proven;
6. rerun Linux and Windows x64 gates;
7. repeat the commercial run.

## Commercial checkpoints

The desired progression is:

1. source/revision validation;
2. executable/runtime entry;
3. first non-black frame;
4. changing/dynamic video;
5. title/menu progression;
6. controller polling;
7. pressed input consumed by the guest;
8. non-silent audio;
9. Memory Card read/write behavior;
10. character/stage selection;
11. fight start;
12. representative fight completion;
13. return/menu transition without a new production frontier.

A checkpoint is marked commercially verified only from a current user-supplied run.

## Frontier priorities

When a run stops, use the current evidence in this order:

- CPU boundary: implement the exact R3000A/COP0/GTE semantic required;
- BIOS call: implement the exact selector and preserve strict unknown-call behavior;
- CD-ROM command: use the cumulative command history plus unsupported command;
- DMA/MMIO: implement the exact address/direction/mode;
- GPU GP0/GP1: implement the exact command and add a raster/VRAM regression;
- fatal runtime error: preserve the final trace and isolate the host/runtime fault.

Do not pre-implement broad PS1 compatibility matrices without a JoJo frontier.

## Completion criteria

Phase 7 is complete only when a fresh supported commercial run demonstrates:

- visible and changing video;
- title/menu progression;
- real controller interaction;
- non-silent audio;
- Memory Card behavior used by the game where applicable;
- entry into and completion of a representative fight;
- no unresolved production frontier on that validated path;
- Linux and Windows x64 full gates green for the corresponding code head.

Phase 7 completion does not yet prove the later native x64 recompiler backend. The R3000A reference runtime remains the semantic oracle until Phase 8 promotion.
