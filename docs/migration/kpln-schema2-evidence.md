# KPLN schema-2 evidence boundary

This document records the provenance boundary for the native KPLN decoder.

## Payload structures used by the importer

The following relationships are treated as format facts and are implemented
without executing the original PlayStation program:

- `0x0800` contains 12-byte direct frame records followed by tile-word
  matrices.
- `0x0801` contains compressed streams that expand to one 16x16 4bpp tile
  (128 bytes).
- `0x0802` contains 12-byte cached frame records, visibility masks and
  32-bit descriptors.
- descriptor low 24 bits address the `0x0801` stream pool.
- descriptor bits 24..29 provide the normal relative CLUT selector.
- descriptor bits 30..31 encode the tile transform.
- `0x0202` is normalized as a 1024x256 4bpp indexed atlas.
- `0x0204` uses repeated 16-pixel-wide upload strips and is normalized to
  a conventional indexed surface.
- `0x0803..0x0807` populate specific regions of the KPLN CLUT window rather
  than representing interchangeable standalone palette banks.

The project contains independent C++ implementations and focused synthetic
tests for these structures.

## Region-specific executable evidence

Public reverse-engineering notes for the European SLES build identify
renderer/animation consumers and absolute executable addresses. Those
absolute addresses are **not assumed to be valid for the USA SLUS build**.

In particular, animation-script call discovery must first identify the USA
consumer by signature/evidence. Until that is done, the importer only uses
data-only structures and conservative PL compact-context candidates that do
not depend on an executable address.

## Runtime rule

All KPLN work happens in the offline content importer. The Godot runtime
consumes native JSON/TGA/WAV resources and does not emulate the PS1 GPU,
R3000A, memory map or loader.
