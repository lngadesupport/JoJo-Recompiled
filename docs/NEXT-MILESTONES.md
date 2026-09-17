# Next milestones

The active guest architecture is PlayStation 1 and the product scope is **JoJo PS1 only**. Current machine-checkable readiness remains in [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified` for the PS1 architecture-clean baseline. The active build/CTest/workflow contains no Dreamcast/SH-4 guest/backend targets.

**R2.2 — Commercial revision enablement** remains `blocked-external-evidence`. The known whole-image fingerprint is recognized, and the user has now produced a bounded local execution report from the supported commercial installation, but that report does not contain the canonical revision-discovery fields required to promote this gate. Commercial game bytes or a proprietary PlayStation BIOS remain prohibited from Git, CI, artifacts and releases.

**R2.3 — Game-specific execution and device integration** remains `implemented-unverified`. Synthetic Linux/Windows contracts cover the M2 R3000A semantics plus JoJo-specific 2 MiB PS1 RAM, 1 KiB scratchpad, explicit supported aliases, transactional PS-X EXE payload placement, installation-backed bounded reference execution, deterministic replay, A0/B0/C0 boundary recognition, MMIO-boundary reporting and instruction-budget termination. The immutable M3A implementation evidence remains GitHub Actions run `34334677057`.

**First commercial checkpoint evidence captured.** The user's first bounded derived report from the validated JoJo installation retired exactly 10,000 instructions and ended with `execution_budget_exhausted`, `last_pc=0x8001001c`, and `last_opcode=0xac400000`. It recorded no BIOS call, CPU boundary, MMIO/unsupported access, interrupt, DMA, GPU command, VRAM write or presented frame. This proves bounded commercial executable progress through the reference executor only; it does not prove boot or identify a BIOS/device service to implement.

**M3C deep evidence capture — ready for the next local run.** The Windows `JOJO-Recompiled.exe` now uses a dedicated installation-backed local-evidence path with a bounded **1,000,000-instruction** budget. The runtime retains only the most recent **16 PC/opcode samples**, and the report writer exports those additive `trace_*` fields to `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`. The code-head is `b099cc5bee6b0ab14301daa966a45b2d9409a6c2`; GitHub Actions run `34411091433` passed both `Portable core / Linux` and `Windows x64 / MSVC 2022`, including the Windows artifact upload.

M3C adds evidence depth only. It does **not** implement PlayStation BIOS/HLE, IRQ/timers, DMA, CD-ROM runtime behavior, GPU rendering, SPU audio, GTE execution beyond the explicit COP2 boundary, or original-game controller behavior. Unsupported boundaries remain explicit diagnostics rather than fabricated success.

The next M3C action is to rerun `EXECUTAR CHECKPOINT` against the already prepared supported JoJo installation and inspect the new bounded report. If it identifies an A0/B0/C0 selector, CPU boundary or MMIO/device address, the next implementation plan must reproduce exactly that boundary synthetically and implement only the smallest JoJo-required service. If it again exhausts the instruction budget, the sixteen `trace_*` samples must first be used to determine whether execution is still moving forward or is trapped in a repeated loop.

MIPS CFG/IR and Windows x64 native lowering/cache remain downstream of visible boot. The R3000A reference executor remains the semantic oracle; native x64 code generation is not a current capability and must not be claimed early.

**R2.4 — Real gameplay integration** remains `not-started`. Commercial boot, BIOS/HLE progress, device progress, rendering, audio, input and gameplay each require independent evidence before promotion.

**R2.5 — Online product modes/M9** retains host-side rollback/networking infrastructure only. It is not proof of commercial-game online integration and is lower priority than getting the base JoJo PS1 game executing correctly.

**R2.6 — Production validation/release** remains `not-started`. Passing synthetic M3A/M3B/M3C evidence-capture contracts and the first bounded commercial checkpoint does not make the commercial game bootable or playable.
