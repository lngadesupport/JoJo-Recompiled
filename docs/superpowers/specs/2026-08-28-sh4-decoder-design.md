# SH-4 Decoder Foundation Design

## Goal

Decode selected 16-bit SH-4 opcodes into a stable, side-effect-free intermediate description that later analysis and recompilation passes can consume.

## Scope

This milestone is intentionally a decoder, not a CPU emulator and not a native code generator. It covers the first control-flow and integer/register instructions needed to build a control-flow graph and extend coverage incrementally.

Initial decoded instructions:

- `NOP`, `RTS`, `RTE`;
- `MOV #imm,Rn`, `ADD #imm,Rn`;
- `MOV Rm,Rn`, `ADD Rm,Rn`, `SUB Rm,Rn`, `CMP/EQ Rm,Rn`;
- `BRA`, `BSR`, `BT`, `BF`, `BT/S`, `BF/S`;
- `JMP @Rn`, `JSR @Rn`;
- `MOV.W @(disp,PC),Rn`, `MOV.L @(disp,PC),Rn`, `MOVA @(disp,PC),R0`.

Unknown encodings remain explicit `unsupported` instructions rather than being guessed.

## Representation

Each instruction retains the raw 16-bit word and decoded operands. Register fields use 0..15. Signed immediates are sign-extended at decode time. Branch displacement is stored in bytes, not encoded units.

The decoder exposes helpers for direct branch targets and PC-relative literal addresses. These helpers use SH-4 PC semantics and do not read memory.

## Byte stream

Dreamcast SH-4 instruction words are decoded from little-endian byte pairs. Odd-sized streams fail rather than silently dropping the last byte.

## Safety and determinism

- pure decoding only;
- no execution or host code generation;
- no access to user game media from this module;
- no global mutable state;
- unsupported opcode is data, not a crash.
