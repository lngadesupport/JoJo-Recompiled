# SH-4 Intermediate Representation Design

## Goal

Lift validated SH-4 CFG blocks into a small, explicit IR that separates guest instruction decoding from future native-code generation.

## Contract

The IR is descriptive and side-effect free while being built. It preserves source addresses, guest registers, immediates, direct targets, block exits, branch/fallthrough edges, and whether an operation belongs to a delay slot.

A control-transfer operation with a delayed SH-4 form appears before its delay-slot operation and the following IR operation is marked `in_delay_slot=true`. This ordering means **latch the transfer condition/target, execute the marked delay-slot operation, then apply the block exit**. Native backends must not transfer host control at the control-transfer IR operation itself.

## Initial operations

NOP; immediate/register arithmetic and copies; equality comparison; direct/conditional branches; direct/indirect calls and jumps; PR/exception returns; PC-relative word/long/address operations.

## Conservative behavior

A CFG block that contains or terminates on an unsupported SH-4 instruction is not liftable yet. The lifter fails explicitly rather than inventing semantics.

## Why this layer exists

The decoder remains architecture-focused, the CFG remains control-flow-focused, and future x64/ARM64/native backends consume the same IR. Deterministic simulation, rollback, debugging and training tools can inspect guest-level operations without depending on host machine code.
