# Dreamcast Boot Analysis Design

## Goal

Connect the discovered Dreamcast boot program to the SH-4 decoder/CFG without executing user code.

## Media classification

The IP metadata `device_info` field is used only for a conservative first classification:

- `GD-ROM...` => boot program bytes are treated as plain executable bytes;
- `CD-ROM...` => MIL-CD/self-boot path may require scrambling normalization, so analysis is refused until that normalizer is implemented;
- anything else => unknown and refused.

No heuristic descrambling is performed.

## Analysis

For plain GD-ROM programs the module:

1. decodes all 16-bit words linearly for coverage diagnostics;
2. counts supported and unsupported words;
3. groups unsupported raw opcodes into a deterministic histogram;
4. builds a reachable CFG from Dreamcast's program load address `0x8C010000`.

The linear coverage count is diagnostic only: executable files can contain inline data and the count must not be interpreted as proof that every word is reachable code.

## Safety

- no execution;
- no native code generation;
- no file writes;
- no blind scrambling transform;
- no game bytes retained outside the caller-owned object.
