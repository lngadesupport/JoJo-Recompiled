# Dreamcast Boot Analysis Implementation Plan

**Goal:** Connect Dreamcast boot discovery to SH-4 coverage and CFG analysis.

### Task 1: media/encoding classification
- [x] Add failing GD-ROM/CD-ROM/unknown tests.
- [x] Implement conservative classifier.

### Task 2: opcode coverage
- [x] Add failing supported/unsupported histogram tests.
- [x] Decode linear word stream and sort histogram deterministically.

### Task 3: entry CFG
- [x] Build CFG at `0x8C010000`.
- [x] Reject MIL-CD/unknown until normalization exists.
- [x] Run complete suite.
