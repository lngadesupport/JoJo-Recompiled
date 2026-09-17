# Disc Filesystem and Revision Identification Design

## Goal

Turn the first-run source image from an opaque byte stream into a bounded, read-only virtual filesystem that later recompiler stages can query without knowing host paths or embedding copyrighted data.

## M2 scope

The first adapter is ISO9660 with 2048-byte logical sectors. It validates the Primary Volume Descriptor, parses directory records with overflow/bounds checks, normalizes ISO version suffixes such as `;1`, traverses nested directories case-insensitively, lists entries, and reads file bytes by extent.

BIN/CUE and GDI remain separate transport/track adapters for a later increment; the filesystem interface must not assume the host file itself is a raw 2048-byte ISO forever.

## Public model

`Iso9660Image` stores only source path plus validated root-directory extent metadata. Public operations:

- `open_iso9660(path)`
- `list_iso9660_directory(image, path)`
- `read_iso9660_file(image, path)`

`DiscFileEntry` exposes normalized name/path, directory flag, extent LBA and byte size. All reads validate multiplication/addition against file size before seeking.

## Revision identification

A revision profile is declarative: stable revision id plus one or more file signatures. Each signature contains a normalized disc path, exact size and FNV-1a64 hash. `identify_game_revision()` evaluates supplied profiles against the mounted image and returns a distinct `unknown_revision` error when none matches.

No retail game hashes or copyrighted bytes are invented in this milestone. Tests construct a synthetic ISO and synthetic revision profile. Real supported-revision signatures will be added only from verifiable metadata derived from a user's legally obtained copy or trustworthy public preservation metadata.

## Security and correctness

- read-only access;
- no path traversal (`..`) accepted from caller paths;
- no extent may escape the source file;
- malformed directory record lengths are errors;
- invalid PVD identifier/version is rejected;
- file reads are capped by validated directory-record sizes;
- tests contain only generated synthetic bytes.
