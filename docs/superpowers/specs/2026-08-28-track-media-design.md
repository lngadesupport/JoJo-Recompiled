# Track-aware Dreamcast Media Design

## Goal

Make the read-only disc filesystem consume logical 2048-byte user-data sectors independently of how a user's dump is stored on disk. Cooked ISO, raw BIN, CUE/BIN and GDI track sets must converge on one bounded sector interface.

## Sector source

`LogicalSectorSource` describes one selected data track without owning copyrighted bytes:

- source file path;
- byte offset of the track inside that file;
- physical sector size (2048 or 2352 in this increment);
- user-data offset within each physical sector (0, 16 or 24);
- logical 2048-byte sector count;
- source format label.

All reads map logical LBAs to physical offsets with overflow and file-bound checks. Raw 2352 mode-1 sectors expose bytes 16..2063. Raw mode-2 form-1 sectors expose bytes 24..2071.

## Discovery

- `.iso`: require a valid cooked 2048-byte PVD at logical sector 16.
- `.bin`: auto-detect cooked 2048, raw 2352 mode 1, or raw 2352 mode 2 form 1 by checking the PVD signature at sector 16.
- `.gdi`: parse descriptor lines, consider data tracks (`type=4`) with 2048/2352-byte sectors, resolve only safe relative filenames, and select the data track whose logical sector 16 contains a valid ISO9660 PVD.
- `.cue`: parse `FILE`, `TRACK` and `INDEX 01` for MODE1/2048, MODE1/2352 or MODE2/2352 tracks, resolve safe relative filenames, and select a track with a valid PVD.

Descriptor paths that are absolute or contain `..` are rejected rather than allowed to escape the descriptor directory.

## ISO9660 integration

`Iso9660Image` stores a `LogicalSectorSource`. Existing `open_iso9660(path)` first discovers a sector source, while an overload accepts a prevalidated source. Directory and file extents are read through logical sectors, so ISO9660 code no longer assumes `offset = LBA * 2048` in the host file.

## Conversion integration

The conversion filesystem-discovery stage calls the track-aware ISO9660 opener for all advertised media extensions. Revision matching therefore operates on internal files identically regardless of ISO/BIN/CUE/GDI packaging.

## Tests

Tests generate a tiny synthetic cooked ISO and wrap it into raw 2352 sectors at runtime. Generated descriptor files point to those synthetic tracks. No game bytes or retail hashes enter source control.
