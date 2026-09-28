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

The KPLN model has been upgraded to **schema 2**. The earlier interpretation
of `0x0800` as a generic group/list table has been retired from code and
tests.

The evidence-backed native formats are now:

- `0x0800`: direct sprite-frame records, 12 bytes per record
- `0x0801`: compressed tile streams; one referenced stream expands to
  **128 bytes = one 16x16 4bpp tile**
- `0x0802`: cached sprite-frame records using visibility bitmasks plus
  32-bit tile descriptors
- cached descriptor low 24 bits: byte offset into `0x0801`
- cached descriptor bits 24..29: relative CLUT selector in the normal cached
  rendering modes
- cached descriptor bits 30..31: tile transform
  (identity / vertical flip / horizontal flip / both)
- `0x0202`: 1024x256 4bpp indexed atlas where present
- `0x0204`: direct VRAM-style 4bpp uploads normalized into horizontal
  16x256 strips instead of exposing VRAM to Godot

The importer now emits native 16x16 decoded tile previews, direct/cached
frame structures, indexed surfaces, and CPU-rendered frame previews.
Neither path submits PS1 GPU commands.

The CLUT model was also corrected. `0x0803..0x0807` are **not five
interchangeable 16-color palette banks**. They populate defined regions of a
native reconstruction of the retail CLUT window:

- window width: **0x180 words (384 colors)**
- window height: **0x18 rows**
- base source row: **0x1e0**
- `0x0803`: fixed 0x100-byte stride per palette ID
- `0x0804..0x0806`: dynamic pools split across the retail palette IDs
- `0x0807`: fixed two-row slabs used by both sides

The renderer accepts recovered CLUT base, signed CLUT mode, CLUT row and
orientation. Negative CLUT modes remain signed; a regression test prevents
the old unsigned-promotion error.

`derived/kpln/KPLNxx/graphics.json` is now schema 2 and can contain:

- direct `0x0800` frames and parts
- cached `0x0802` frames and parts
- decompressed `0x0801` tile previews
- native `0x0202` / `0x0204` indexed surfaces
- reconstructed CLUT-window previews
- default-context direct/cached frame previews

The original USA-disc inventory and PAC/TIM counts above remain retail
validated. The new KPLN schema-2 implementation is covered by focused
decoder/renderer tests and the Godot validation gate; a fresh complete
schema-2 import of the USA BIN is still required before publishing global
schema-2 retail frame/tile counts.


## Fighter native cross-link layer

The importer emits a conservative native relation report for every fighter
that has the required HIT + TKC/TKD + KPLN sources:

`derived/fighters/<ID>/native_links.json`

The report uses schema 2 and deliberately labels unresolved relationships as
candidates:

- TKC `reference_index` -> candidate HIT-table index
- in-range/non-empty HIT evidence plus candidate rectangle geometry
- TKD `element_index` -> candidate **direct `0x0800` frame** index
- TKD `element_index` -> candidate **cached `0x0802` frame** index
- independent bounds/coverage counters for the two KPLN frame spaces

The importer also scans `PLxx.BIN` for conservative 0x28-byte compact render
context candidates without executing MIPS. Candidate fields include:

- frame index
- signed CLUT mode
- CLUT base
- CLUT row
- asset slot
- flip/orientation fields
- source offset and confidence score

For each frame the highest-scoring compact candidate can be used to produce
diagnostic direct/cached previews for palette IDs 0 and 1. These previews are
evidence for reverse engineering; the candidate scanner is not promoted to
gameplay semantics until its consumer relationship is proven.

Godot exposes the report through `JojoFighterResource` and
`JojoFighterSlotResource`. Each imported fighter can be addressed as
26 native slots without reading source BIN/PAC files at runtime.


## Migration Inspector

The native Godot frontend exposes a migration inspector under `DEV TOOLS`.

It can select fighter and one of the 26 native slots, then inspect:

- TKC/TKD record counts and candidate HIT references
- independent TKD -> direct/cached frame coverage
- default cached-frame previews
- default direct-frame previews
- best recovered PL-context cached/direct previews
- normalized indexed surfaces
- reconstructed CLUT windows
- fallback converted TIM visuals

This tool is specifically for proving the remaining animation/render
relationships visually while preserving the content-only runtime rule.


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
- conservative TKC->HIT and TKD->KPLN cross-link reports
- typed 26-slot `JojoFighterSlotResource` access
- DEV TOOLS fighter migration inspector
- native indexed-page texture loading
- native palette-bank texture loading
- `indexed_palette.gdshader` for applying converted 16-color palettes to
  converted 4bpp index pages
- native frontend scaffold
- no PS1 runtime dependency

## Next frontiers

1. Run the complete schema-2 importer against the USA retail BIN and publish
   global direct/cached frame, tile, CLUT and context-candidate counts.
2. Prove whether TKC `reference_index` is semantically the HIT selector.
3. Prove which KPLN frame space(s) TKD `element_index` selects in gameplay.
4. Identify USA animation-interpreter call sites/signatures in `PLxx.BIN`
   before using any region-specific absolute function address.
5. Convert proven PL animation-script sequences into native Godot animation
   resources.
6. Split remaining mixed PL overlays into proven content tables and discard
   executable MIPS portions from final runtime data.
7. Decode stages, UI/story/event tables and remaining PAC resource families.
8. Validate converted assets against reference output and stop shipping
   source-format intermediates for each completed family.
