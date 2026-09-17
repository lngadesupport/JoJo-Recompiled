# Phase 4A — COP2/GTE register foundation

Status: GREEN

- RED: GitHub Actions job `105245153983` failed because `R3000aState` had no `gte` member.
- Production: `R3000aGte` owns 32 data registers and 32 control registers.
- MFC2/CFC2 use the existing R3000A delayed-load path.
- MTC2/CTC2 write GTE registers immediately.
- CU2-disabled operations still raise Coprocessor Unusable with CE=2.
- COP2 command execution remains an explicit `cop2_unimplemented` frontier for Phase 4B.
- GREEN: job `105249645678` passed COP2, COP0, exception, and boundary regressions.
