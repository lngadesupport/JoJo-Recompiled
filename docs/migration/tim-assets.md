# TIM assets in the retail PAC data

The content-only migration has confirmed standard PlayStation TIM image
records inside several PAC resource families. These are treated purely as
source image formats; the native Godot runtime does not emulate the PS1 GPU.

## Discovery

Scanning all 2,656 PAC chunks at their 2048-byte source alignment found:

- **375** valid TIM records
- **648** decoded image variants after palette expansion
- 4bpp CLUT images and 8bpp CLUT images in the retail data
- repeated TIM records inside large event/portrait bundles

Confirmed PAC resource families containing TIM records include:

- `0x0106`
- `0x0109`
- `0x010d`
- `0x010f`
- `0x0118`
- `0x0119`
- `0x011c`
- `0x011d`
- `0x0120`

Additional isolated records occur in a small number of other resource
families.

## Conversion

`tim_image.cpp` validates the normal TIM header and block layout, then
converts source pixels directly into RGBA8.

Supported source modes:

- 4bpp indexed + CLUT
- 8bpp indexed + CLUT
- 16bpp BGR555 direct color
- 24bpp direct color

For indexed TIMs every embedded palette is expanded into a separate native
image. The importer writes the resulting images as 32-bit TGA files under:

```text
derived/tim/<PAC_NAME>/
```

Each PAC `pack.json` records:

- source TIM offset
- TIM flags
- width/height
- palette index/count
- output TGA path
- whether any source color carries the PS1 STP bit

## Transparency policy

Color value zero becomes transparent in the RGBA export. The PS1 STP bit is
recorded as metadata instead of being treated as ordinary alpha, because STP
only becomes semitransparency when the original primitive state requests it.

This keeps the source information available for later material/effect
conversion without importing PS1 GPU blending behavior into the Godot
runtime.
