# Godot Content-Only Migration State

Branch: `migration/godot-content-only`

## Policy

The new Godot runtime imports game content only.

It must not execute or emulate:

- R3000A/MIPS
- PlayStation BIOS
- PS1 GPU command stream
- SPU
- SIO
- CD-ROM timing
- DMA/timers
- PS1 memory map

Original-disc parsing is restricted to the offline importer.

## Implemented

- Godot 4.7.2 content-only project scaffold.
- 240 FPS presentation target in the new project.
- Native content registry reading `content/manifest.json`.
- Standalone `jojo_content_importer`.
- Importer links only media/ISO9660/content migration code, not `jojo_core`.
- Recursive BIN/CUE/ISO extraction.
- Runtime/system exclusion list.
- Content classification.
- CLT BGR555 palette preview conversion to TGA.
- Candidate string extraction from M/*.BIN.
- PAC container parser and splitter.
- Focused Content Migration Gate workflow.

## Retail validation

Validated using the user-provided USA disc image:

- source format: `bin-mode2-2352`
- content files imported: **1,072**
- runtime/system files excluded: **3**
- bytes imported: **542,289,549**
- graphics PACs: **750**
- PAC manifests produced: **750**
- PAC chunks produced: **2,656**
- malformed PACs: **0**
- distinct PAC resource IDs: **93**
- CLT palette previews: **2**
- BIN files with extracted candidate text: **171**

Excluded from the native content tree:

- `/SLUS_010.60`
- `/SYSTEM.CNF`
- `/ZNULL.DAT`

## Next conversion frontiers

1. Map PAC resource IDs to texture/palette/sprite/animation semantics.
2. Decode graphics payload compression/indexing into native image resources.
3. Decode `PLxx*.BIN` character data.
4. Decode `*_HIT.BIN` hit/hurt boxes.
5. Decode UI/story/menu tables.
6. Convert XA streams to native audio.
7. Build native character/stage/resource catalogs for Godot.
8. Validate each converted asset against the legacy rendering reference.
