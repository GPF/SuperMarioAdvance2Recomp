# SMA2 runtime SH-4 dynarec — physical Dreamcast bounded diagnostic (2026-10-20)

**Verdict: PASS (guest diagnostic), one run, no retries.** Not gameplay and not general Dreamcast support.

- Console `192.168.0.128` via `/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh`
  (task `sma2-dynarec-hw-20261020`; lock absent before, released after; no stray loader).
  KOS environ sourced, GCC 17 at `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc`. Ping before launch: 1 of 2 replies.
- ELF: stripped, SHA-256 `347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5`, built from the
  published migration branch clone (GBARecomp 2d2a8fe, arm-recomp-core 61cdc51a). Media (copies) hashes in
  `evidence/sma2_dynarec_hardware_20261020/prelaunch.sha256`; `/pc/sma2_cpu_backend.txt` = `j`.
- Execution-path evidence (console log): `sma2_cpu_backend=sh4-dynarec`, `[sma2-jit] backend init succeeded`,
  stage1/2/3/D/E each `PASS` with blocks=1 fallback=0 mismatches=0 cpsr=match iwram=match and native cycles equal
  to interpreter cycles (58/43/46/57/43), then `Program returned 0`. The static dispatch table was empty, so
  static AOT could not have run.
- Same `firstdiv status=INELIGIBLE` note as nullDC3: the extra first-divergence probe stops on comparison
  eligibility; it is not one of the five required stages.
- Limits: five small blocks; EWRAM is not in the JIT comparison; the 343-entry AOT path was not re-run here.
