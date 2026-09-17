# JOJO Recompiled — R2.3 Maple DMA-First Design

## Goal

Add the first truthful Maple DMA execution path to the Dreamcast reference boot runtime without inventing a controller, VMU, commercial-game behavior, or asynchronous hardware timing that has not been implemented.

## Scope

This subincrement extends the existing `DreamcastMaple` MMIO boundary so a software-triggered DMA can consume a syntactically valid Maple command table from Dreamcast main RAM, produce the documented no-device response, return `SB_MDST` to idle, and raise the System ASIC Maple-DMA-complete normal interrupt.

The work is deliberately narrower than full R2.3. It does not establish commercial-game compatibility and must not promote R2.3 to `verified`.

## Hardware contract used by this increment

The implementation follows the public Dreamcast/KallistiOS Maple register model already used by independent software:

- `SB_MDSTAR` (`0x005F6C04`) selects the command-table start address in Dreamcast main RAM.
- `SB_MDTSEL` (`0x005F6C10`) selects software (`0`) or hardware/VBlank (`1`) triggering.
- `SB_MDEN` (`0x005F6C14`) enables Maple DMA when bit 0 is set.
- `SB_MDST` (`0x005F6C18`) reads `0` when idle and `1` while DMA is active; writing `1` starts a software-triggered transfer when enabled.
- Normal interrupt status bit 12 represents Maple DMA completion.
- A command-table entry starts with a transfer-control word, followed by a receive-buffer address and the Maple frame words. Bit 31 of the transfer-control word marks the final table entry. Bits 17:16 select the Maple port. The low byte is the count of additional frame payload words after the mandatory Maple frame header.
- With no endpoint attached, the documented receive result is `0xFFFFFFFF`.

This increment supports exactly one final command-table entry. Chained/multiple entries remain explicitly unsupported until a later test requires them.

## Architecture

`DreamcastMaple` becomes a small synchronous DMA engine owned by the boot runner. It receives references to `DreamcastExecutableMemory` and `DreamcastSystemAsic`; no second memory subsystem or event loop is introduced.

On an accepted `SB_MDST=1` software trigger the device:

1. validates enable/trigger state and `MDSTAR` alignment/range;
2. marks `MDST` busy;
3. reads the three mandatory command-table words from main RAM;
4. validates the single-entry descriptor and frame span before performing side effects;
5. validates the receive address as writable Dreamcast main RAM;
6. writes `0xFFFFFFFF` to the receive buffer because no Maple endpoint exists yet;
7. returns `MDST` to idle; and
8. raises normal System ASIC bit 12.

All failures are deterministic `Result` failures. The device must return to idle on a failed start and must not raise the completion interrupt after malformed or unsupported DMA input.

## Supported descriptor subset

For this increment a valid table entry must satisfy all of the following:

- `MDSTAR` is 32-byte aligned and in physical/cached/uncached Dreamcast main RAM.
- Descriptor bit 31 is set, because only a single final entry is supported.
- Port field bits 17:16 are in the hardware range 0..3.
- Reserved transfer-control bits outside the final flag, port field, and low-byte payload count are zero.
- The descriptor, receive address, frame header, and declared payload words all fit inside mapped main RAM without address wrap.
- The receive address is 32-bit aligned and writable main RAM.

The mandatory frame header word is parsed only for structural validity in this subincrement. No command is dispatched to a controller/VMU endpoint.

## Register behavior

- Existing `MDSTAR`, `MDTSEL`, and `MDEN` masks remain intact.
- `MDST` becomes readable.
- Writing `MDST=0` is a no-op.
- Writing `MDST=1` while Maple is disabled fails explicitly.
- Writing `MDST=1` with `MDTSEL=1` fails explicitly because hardware/VBlank triggering is outside this subincrement.
- Writing any unsupported bits/registers continues to fail explicitly.

## Interrupt behavior

Successful DMA completion calls `DreamcastSystemAsic::raise_normal(1u << 12u)`. Existing System ASIC mask/IRQ delivery remains the sole path that decides whether the SH-4 observes an interrupt level.

No completion IRQ is raised for malformed descriptors, invalid addresses, disabled DMA, unsupported trigger mode, or unsupported multi-entry command tables.

## Testing strategy

TDD evidence must include:

1. A RED boot-runner integration test where `MDEN=1`, `MDSTAR` points at a valid single descriptor, and `MDST=1`; before implementation it fails because `MDST` is unsupported.
2. GREEN proof that the receive buffer becomes `0xFFFFFFFF`, the boot program continues, and no bus fault is reported.
3. Direct Maple tests for disabled DMA, hardware trigger rejection, malformed/final-bit validation, invalid receive address, `MDST` idle after success/failure, and completion interrupt bit 12.
4. Existing Linux and Windows/MSVC suites plus the production-readiness gate on the exact final commit.
5. Windows artifact upload remains the single `JOJO-Recompiled.exe` product artifact.

## Truth/status policy

R2.2 remains `blocked-external-evidence` until legally supplied supported commercial media exists. R2.3 remains `implemented-unverified`; this generic Maple DMA fixture is implementation evidence, not evidence that the supported commercial title exercises or accepts this path.

## Explicit non-goals

- Controller endpoint or input state.
- VMU, rumble, microphone, keyboard, or other Maple devices.
- Multi-entry/chained command tables.
- VBlank/hardware-triggered Maple DMA.
- Cycle-accurate Maple bus latency or asynchronous completion.
- Commercial media fingerprints or game-specific assumptions.
