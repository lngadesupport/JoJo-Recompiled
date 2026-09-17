# SH-4 Basic Block / CFG Design

## Goal

Turn decoded SH-4 instructions into reachable basic blocks with explicit control-flow edges suitable for later lifting and recompilation.

## Rules

- direct branches (`BRA`, `BT`, `BF`, `BT/S`, `BF/S`) create branch edges;
- conditional branches also create a fallthrough edge;
- `BSR` records a direct call target and continues after its delay slot;
- `JSR` records an indirect call site and continues after its delay slot;
- `JMP`, `RTS`, and `RTE` terminate a block after their delay slot;
- unsupported instructions terminate analysis of that block rather than guessing semantics;
- branch targets into delay-slot instructions are rejected in this first CFG model;
- direct targets outside the supplied image are preserved as external edges, not dereferenced.

## Delay slots

Instructions with delay slots include the instruction at `PC+2` in the terminating block. Missing delay-slot bytes are an error.

## Reachability

The CFG begins at one explicit entry address. Direct branch/fallthrough successors inside the supplied stream are traversed. Call targets are recorded but are not recursively treated as part of the current function CFG.

## Determinism

Blocks and target lists are returned sorted by address so output does not depend on hash/set iteration order.
