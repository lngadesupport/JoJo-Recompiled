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
- 26 fighter IDs (`00` through `19`) have HIT tables
- **6,344 non-empty retail records** were checked globally

The four signed 16-bit fields now have stable geometry semantics:

`x_offset, width, y_offset, height`

Across the 6,344 non-empty retail records, `width` and `height` are
positive in 6,343 cases each; `x_offset` is usually negative and
`y_offset` legitimately crosses zero. Degenerate zero-size retail entries
are preserved rather than discarded.

The Godot fighter resource exposes these imported rectangles as native
`Rect2` values.

## Fighter TK data

24 fighter IDs contain paired `PLxx_TKC.BIN` / `PLxx_TKD.BIN`.

Confirmed retail structure:

- exactly **26 live slots** per pair
- TKC carries a 27th pointer marking the logical end
- TKC original load base: `0x8010D800`
- TKC roots are normalized from PS1 pointers to file offsets
- each non-empty TKC root points to a pointer list terminated by
  `0xFFFFFFFF`
- TKC leaf records are **10 bytes**
- **3,558 TKC leaf references** were checked on the retail disc
- the low byte of TKC word 0 is **always opcode `0x8A`**
- the high byte of TKC word 0 is exported as a `variant`
- TKC word 1 is split into a **14-bit reference index** plus
  `0x4000/0x8000` flags; every retail reference index is `0..278`
- words 2..4 are preserved as explicit parameters until their gameplay
  meaning is proven

TKD begins with a block-size word followed by 26 slot offsets:

- the block size is always a non-zero multiple of 8
- each TKD record is **8 bytes**
- **5,226 TKD records** were checked globally
- record words are normalized as `offset_x`, `offset_y`,
  `element_index`, and `element_flags`
- the packed element uses a **12-bit index (`0..16`) + 4 flag bits**
- the fourth TKD word is reserved and is zero in all 5,226 retail records

The importer exports this structure as native JSON with no absolute PS1
addresses.

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
- all converted TIM/TGA visuals found under those associated PACs

Names are not guessed; the catalog keeps retail hexadecimal IDs until
identity mapping is proven.

Godot wraps each catalog entry in a typed `JojoFighterResource`, with lazy
loading for overlay/TK/HIT JSON, native `Texture2D` resolution for migrated
visuals, and native `Rect2` access for HIT geometry.

## XA audio

The 12 XA files reside in raw Mode-2/Form-2 sectors.

Confirmed source families:

- M00-M08: XA ADPCM, stereo, **37,800 Hz**, up to 8 multiplexed channels
- M10-M12: XA ADPCM, stereo, **18,900 Hz**, up to 16 multiplexed channels

The content importer strips CD sync/header/subheader/EDC framing, demultiplexes
the XA channels, decodes normal retail 4-bit XA ADPCM, and writes **PCM16 WAV**
per channel. The intermediate `.xaadpcm` payload is retained only for
migration validation.

The decoder follows the retail XA 18x128-byte sector layout with four
predictive filters and independent channel history. Godot consumes WAV, so
neither SPU/CD-ROM emulation nor an XA decoder is needed at runtime.

## KPLN fighter graphics

The retail `KPLNxx.PAC` family now has a native structural decoder.

Validated across all **26 KPLN fighter packs**:

- **26** native `derived/kpln/KPLNxx/graphics.json` files
- **2,425** records decoded from resource `0x0800`
- every `0x0800` record is six 16-bit words
- word 0 points into a trailing uint16 index list
- those lists use `0xFFFF` termination; the final PL16 list ends exactly at
  EOF and is preserved as the retail edge case
- word 5 is zero in every decoded retail record
- words 1..4 remain conservatively named until their exact sprite semantics
  are proven

Resources `0x0803..0x0807` are structurally confirmed as 16-color BGR555
palette banks:

- **130** palette banks exported across the 26 fighters
- each bank is an exact multiple of 32 bytes
- every 32-byte unit is 16 BGR555 colors
- previews are exported as native RGBA TGA grids

Resource `0x0202` is present for **11 fighters** and is the exact
**131,072-byte** size required for a 1024x256 4bpp indexed page. The importer
expands each nibble into a native index image and exports a grayscale TGA
index preview. Variable-size `0x0204` graphics remain unresolved and are not
misclassified as `0x0202`.

## Godot native project

Target: **Godot 4.7.2 stable**

Current native project provides:

- 240 FPS presentation target
- content manifest registry
- fighter catalog registry
- typed `JojoFighterResource`
- lazy native texture loading for migrated fighter visuals
- native `Rect2` exposure for migrated HIT geometry
- KPLN native graphics metadata access
- native indexed-page texture loading
- native palette-bank texture loading
- `indexed_palette.gdshader` for applying converted 16-color palettes to
  converted 4bpp index pages
- native frontend scaffold
- no PS1 runtime dependency

## Next frontiers

1. Prove whether TKC `reference_index` directly selects the HIT rectangle
   table and identify the remaining TKC parameters.
2. Identify the semantic meaning of TKD element indices/flags and connect them
   to converted fighter visuals.
3. Split mixed PL overlays into proven content tables and discard executable
   MIPS portions from final runtime data.
4. Build native animation/state resources from TKC/TKD/HIT links.
5. Finish the KPLN sprite chain by decoding `0x0801/0x0802` and proving
   the meanings of `0x0800` words 1..4.
6. Decode variable-size KPLN `0x0204` graphics for the remaining 15
   fighters.
7. Identify sprite-sheet/animation metadata around converted TIM images.
8. Decode stages, UI/story/event tables and remaining PAC resource families.
7. Convert WAV masters to final streaming/distribution formats where useful.
8. Validate converted assets against reference output, then stop shipping
   source-format intermediates for each completed family.
