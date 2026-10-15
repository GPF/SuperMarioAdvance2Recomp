# SMA2 PC Runtime Baseline

## Environment

The supplied run was performed on the user's Ubuntu/Linux PC. The controller label `ZhiXu Controller (Dinput For Windows)` is not evidence of a Windows host. The exact Ubuntu release, kernel, CPU, and executable build identity were not supplied.

## Observed run

The user-reported command was:

```sh
cd /tmp/SMA2-PC-Trial
./build/SuperMarioWorldRecomp \
  --bios gbarecomp/bios/gba_bios.bin \
  --rom roms/super_mario_world_usa.gba \
  game.toml
```

Reported summary: `cpu_backend=static-recompiled`, `FULLY_STATIC`, zero dispatch misses/interpreted/healed instructions, `frames_presented=5077`, `unmapped=0`, and `io_unhandled=0`. The log also reports `cycles=0` and separately reports idle-loop elision; their relationship has not been source-verified as a timing benchmark.

## What this establishes

This is evidence for the observed Ubuntu host run only. It does not establish complete-game coverage, SH-4 compatibility, Dreamcast performance, KOS integration, or Dreamcast hardware support. Preserve the original user-provided log alongside future comparisons if available.
