# Godot Content-Only Migration State

Branch: `migration/godot-content-only`

## Runtime policy

The native Godot runtime imports game content only. It must not execute or
emulate R3000A/MIPS, PlayStation BIOS, GPU command streams, SPU, SIO,
CD-ROM timing, DMA/timers, or the PS1 memory map.

Original-disc parsing and format conversion belong to the offline importer.

## Retail source inventory

Validated against the user-provided USA disc image:

- source format: `bin-mode2-2352`
- files on disc: **1,075**
- content files selected: **1,072**
- runtime/system files excluded: **3**
- selected content bytes: **542,289,549**
- PAC files: **750**
- M/*.BIN files: **306**
- XA files: **12**
- CLT palettes: **2**
- FIN metadata files: **2**

Excluded:

- `/SLUS_010.60`
- `/SYSTEM.CNF`
- `/ZNULL.DAT`

## PAC / graphics migration

All **750/750** retail PAC archives parse with the same container format.

- PAC chunks: **2,656**
- distinct resource IDs: **93**
- malformed PACs: **0**
- sector-aligned standard TIM records found: **375**
- native images after palette expansion: **648**

TIM conversion supports 4bpp, 8bpp, 16bpp and 24bpp sources and writes
RGBA32 TGA images plus palette/STP metadata. Godot consumes the converted
image resources rather than emulating the PS1 GPU.

## Fighter HIT data

Every `PLxx_HIT.BIN` is exactly 4096 bytes:

- **512 records**
- **8 bytes per record**
- each record is preserved as four signed 16-bit fields
- 26 fighter IDs (`00` through `19`) have HIT tables

Field names remain intentionally unresolved until their consumers in the
fighter overlay are proven.

## Fighter TK data

24 fighter IDs contain paired `PLxx_TKC.BIN` / `PLxx_TKD.BIN`.

Confirmed retail structure:

- exactly **27 root slots** per pair
- TKC original load base: `0x8010D800`
- TKC roots are normalized from PS1 pointers to file offsets
- each non-empty TKC root points to a pointer list terminated by
  `0xFFFFFFFF`
- leaf records are **10 bytes = five uint16 fields**
- TKD exposes 27 aligned in-file root values; exact gameplay semantics are
  still being resolved

The importer exports the roots and resolved TKC leaf records as native JSON.

## Fighter PL/PLX overlay pairs

All fighter primary overlays load at the confirmed retail base
`0x800DF000`.

The paired `PLxxX.BIN` form relocates the same overlay by exactly:

`0x00015800`

The importer compares every PL/PLX pair and exports verified direct internal
relocations as native `field_offset -> target_offset` records. This removes
absolute PS1 addresses from the analysis representation. Residual pair
differences are retained as diagnostics because some executable MIPS
immediates are relocated separately; executable code is not intended for the
Godot runtime.

## Fighter catalog

The importer emits:

`derived/fighters/catalog.json`

It contains **26 retail fighter IDs** (`00`..`19`) and links, where
available:

- normalized PL/PLX overlay analysis
- HIT table
- TK data
- explicitly ID-tagged PAC families such as `KPLNxx`, `PLKxx`,
  `KOP_PLxx`, `KACCNTxx`, `KACENDxx`, `KRAxx`, and `KSYOxx`

Names are not guessed; the catalog keeps retail hexadecimal IDs until
identity mapping is proven.

## XA audio

The 12 XA files reside in raw Mode-2/Form-2 sectors.

Confirmed source families:

- M00-M08: XA ADPCM, stereo, **37,800 Hz**, up to 8 multiplexed channels
- M10-M12: XA ADPCM, stereo, **18,900 Hz**, up to 16 multiplexed channels

The content importer strips CD sync/header/subheader/EDC framing and exports
per-channel `.xaadpcm` payload streams plus JSON metadata containing coding,
sample rate, channel count, source-sector ordering and EOF markers.

This is source-audio conversion only; no SPU/CD-ROM emulation is used by
Godot.

## Godot native project

Target: **Godot 4.7.2 stable**

Current native project provides:

- 240 FPS presentation target
- content manifest registry
- fighter catalog registry
- native frontend scaffold
- no PS1 runtime dependency

## Next frontiers

1. Decode semantics of the five TKC leaf fields and TKD records.
2. Split mixed PL overlays into proven content tables and discard executable
   MIPS portions from final runtime data.
3. Map HIT records to animation/state references.
4. Convert XA ADPCM payloads to PCM/OGG resources.
5. Identify sprite-sheet/animation metadata around converted TIM images.
6. Build native Godot fighter resources from overlay/TK/HIT/PAC links.
7. Decode stages, UI/story/event tables and remaining PAC resource families.
8. Validate converted assets against reference output, then stop shipping
   source-format intermediates for each completed family.
