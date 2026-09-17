# Dreamcast Bootstrap Discovery Design

## Goal

Discover the Dreamcast program selected by the disc bootstrap without hardcoding `1ST_READ.BIN`, and expose only bounded metadata/bytes to later recompiler stages.

## Inputs

The subsystem consumes an already validated `Iso9660Image`. Its `LogicalSectorSource` abstracts ISO/BIN/CUE/GDI packaging, so bootstrap reads are always logical 2048-byte sectors.

## IP metadata

The Dreamcast system area occupies the 16 logical sectors before the ISO9660 Primary Volume Descriptor. Metadata is parsed from the first 256 bytes using fixed-width, space-padded ASCII fields:

- hardware id at `0x000..0x00F`;
- maker id at `0x010..0x01F`;
- device information at `0x020..0x02F`;
- area symbols at `0x030..0x037`;
- peripherals at `0x038..0x03F`;
- product number at `0x040..0x049`;
- product version at `0x04A..0x04F`;
- release field at `0x050..0x05F`;
- boot filename at `0x060..0x06F`;
- company name at `0x070..0x07F`;
- software name at `0x080..0x0FF`.

A valid retail/homebrew Dreamcast bootstrap must begin with `SEGA SEGAKATANA` after fixed-field trimming. The boot filename must be a simple filename: empty values, `/`, `\\`, and parent traversal are rejected.

## Boot program

`read_dreamcast_boot_program()` resolves the parsed boot filename through the existing ISO9660 path lookup, requires a regular file, bounds its size, and returns the bytes plus a deterministic FNV-1a64 hash. It does not descramble or execute the program in this milestone.

## Safety

- read-only;
- no host paths are accepted from IP metadata;
- program size is capped before allocation;
- malformed/non-Dreamcast system areas fail explicitly;
- tests use only generated synthetic bytes.

## Next boundary

The next subsystem will classify whether the discovered program is already plain SH-4 code or requires Dreamcast scrambling transformation, then feed normalized bytes to the SH-4 decoder. That classification is deliberately not guessed here.
