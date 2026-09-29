# Retail content inventory and PAC container notes

## Disc content

The USA disc contains 1,075 files and 4 directories.

The content-only importer keeps 1,072 files and excludes exactly:

- `/SLUS_010.60` — original PS1 executable
- `/SYSTEM.CNF` — PS1 boot configuration
- `/ZNULL.DAT` — non-gameplay padding/null data

Imported content:

- 750 `.PAC` files under `/P`
- 306 `.BIN` files under `/M`
- 12 `.XA` files under `/X`
- 2 `.CLT` palettes under `/C`
- 2 `.FIN` metadata files under `/C`

## PAC container

All 750 retail PAC files follow the same lossless container layout.

```text
offset 0x0000  u32 chunk_count
offset 0x0004  u32 total_file_size

repeat chunk_count times:
  u32 resource_id
  u32 payload_size

offset 0x0800  chunk 0 payload
               padding to 2048-byte boundary
               chunk 1 payload
               padding to 2048-byte boundary
               ...
```

The first 2048-byte sector is therefore a table/index sector. Every payload is
stored sequentially and padded independently to a 2048-byte boundary.

Validation against the retail disc:

- PAC files checked: **750 / 750**
- structurally valid: **750**
- malformed: **0**
- total chunks: **2,656**
- distinct resource IDs: **93**

Most frequent resource IDs:

| Resource ID | Occurrences |
| --- | ---: |
| `0x0101` | 266 |
| `0x0122` | 240 |
| `0x0800` | 190 |
| `0x0801` | 189 |
| `0x0803` | 110 |
| `0x0804` | 103 |
| `0x0201` | 100 |
| `0x3401` | 100 |
| `0x1401` | 100 |
| `0x2401` | 100 |
| `0x0103` | 80 |
| `0x0109` | 72 |
| `0x0805` | 69 |
| `0x0102` | 69 |
| `0x0106` | 68 |
| `0x0100` | 62 |
| `0x0806` | 58 |
| `0x0802` | 57 |

The importer writes each PAC to:

```text
derived/pac/<PACK_NAME>/
  pack.json
  000_type_XXXX.bin
  001_type_XXXX.bin
  ...
```

This is intentionally lossless. The next migration layer maps each resource
ID family to native textures, sprite sheets, animation metadata, or other
Godot resources.

## Important architecture rule

The resource ID decoder may understand original *data formats*, but it must
not reproduce PlayStation hardware behavior. For example:

- BGR555 -> RGBA conversion is allowed.
- Sprite/animation command tables -> Godot resources are allowed.
- Recreating PS1 GPU command execution is not allowed in the new runtime.
- XA -> PCM/stream conversion is allowed.
- SPU emulation is not allowed.
