# SH-4 unsupported-opcode census implementation plan

## Goal
Add a backend-neutral analyzer that scans SH-4 instruction words and reports supported versus unsupported opcode occurrences, including frequency and representative addresses, so instruction implementation can be prioritized from real executable data when the user supplies it.

## Architecture
The census belongs in `jojo_core` and consumes raw instruction bytes plus a base address. It reuses `decode_sh4_stream` rather than duplicating decoder logic. The result contains total decoded words, supported count, unsupported count, and groups unsupported raw opcode words by frequency with bounded sample addresses. No game assets or executable bytes are stored in the repository.

## TDD steps
1. Add a failing unit test with a synthetic byte stream containing repeated supported and unsupported opcodes; assert totals, grouping, frequency ordering and sample addresses.
2. Add `Sh4OpcodeCensusEntry`, `Sh4OpcodeCensus` and `analyze_sh4_opcode_census(...)` declarations.
3. Implement analysis by decoding each aligned 16-bit word at its address and accumulating unsupported raws.
4. Sort unsupported entries by descending count, then raw opcode for deterministic ties.
5. Reject odd byte counts and 32-bit address-range overflow consistently with the decoder.
6. Register the test in CMake and run the complete Linux/Windows CI gates.
7. Later wire the census into Dreamcast executable analysis so a user-provided game image can generate a prioritized unsupported-opcode report without embedding copyrighted data.
