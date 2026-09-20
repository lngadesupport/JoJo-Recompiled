# Content-only Godot migration

## Rule

The new runtime imports **game content**, not PlayStation hardware.

The Godot project must not depend on:

- R3000A/MIPS execution
- PlayStation BIOS/HLE
- PS1 GPU command emulation or VRAM-as-runtime rendering
- SPU emulation
- SIO/controller emulation
- CD-ROM timing
- DMA/timer emulation
- PS1 memory map

The legacy PS1 code remains useful only as a reference/validation path while
content is decoded.

## Actual USA disc inventory

The source image currently contains 1,075 files:

| Source | Count | Migration treatment |
| --- | ---: | --- |
| `/P/*.PAC` | 750 | graphics/content packs; migrate |
| `/M/*.BIN` | 306 | character, hitbox, UI, script/data; migrate |
| `/X/*.XA` | 12 | audio streams; migrate/convert |
| `/C/*.CLT` | 2 | 256-color BGR555 palettes; migrate |
| `/C/*.FIN` | 2 | color/render metadata; migrate |
| `SLUS_010.60` | 1 | exclude |
| `SYSTEM.CNF` | 1 | exclude |
| `ZNULL.DAT` | 1 | exclude/padding |

That leaves **1,072 content files** in the migration pipeline.

## Pipeline

```text
User BIN/CUE/ISO
      |
      v
jojo_content_importer       (offline only)
      |
      +-- raw/              faithful extracted source content
      +-- derived/          converted/decoded assets
      +-- manifest.json
      |
      v
engine/godot/content
      |
      v
Godot native runtime
```

Godot never opens the original disc and never boots the PS1 executable.

## Conversion order

1. Inventory and lossless extraction.
2. Decode `.PAC` graphics archives into textures/sprite metadata.
3. Decode character `PLxx*.BIN` tables into native resources.
4. Decode `*_HIT.BIN` into hitbox/hurtbox resources.
5. Decode menu/story/ending data and text.
6. Decode XA audio to modern PCM/streaming assets.
7. Build native scenes/resources from the manifest.
8. Validate converted output against the legacy runtime screenshots/hashes.
9. Remove each corresponding PS1-path dependency after parity is established.

## Engine baseline

Use Godot 4.7.2 stable. The project currently caps presentation at 240 FPS
with VSync disabled by default. Simulation timing will be defined separately
from presentation timing once native gameplay migration starts.
