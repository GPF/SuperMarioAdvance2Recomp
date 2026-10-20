# Vendored snapshot: Bruce's mGBA ARM/Thumb -> SH-4 dynarec

- Source repository: `/home/gpf/code/dreamcast/brucemGBA` (read-only; never modified by this build)
- Source revision: `f3eb1bbad6dfd5be2c48ee599d1bbcf7ba33f41b`
- License: Mozilla Public License, v. 2.0 (`LICENSE` at the source root)
- Snapshot command: `tools/vendor_mgba_sh4_jit.sh`

## What was copied

Translation engine (`src/arm/jit`):
- src/arm/jit/jit-private.h
- src/arm/jit/sh4-emit.h
- src/arm/jit/sh4-interp.h
- src/arm/jit/jit.c
- src/arm/jit/jit-emit.c
- src/arm/jit/jit-host.c
- src/arm/jit/fastmem.c

ARM/Thumb core the engine falls back to (`src/arm`):
- src/arm/arm.c
- src/arm/isa-arm.c
- src/arm/isa-thumb.c
- src/arm/decoder.c
- src/arm/decoder-arm.c
- src/arm/decoder-thumb.c

## SMA2-local adaptations applied to the snapshot

The upstream repository is never modified; these edits are re-applied by this
script every time the snapshot is produced, and are guarded so they are inert
without the SMA2 build's defines:

- `src/arm/jit/jit-emit.c` `_emitMemoryStub` / `_emitMultipleStub`: under
  `SMA2_JIT_SLOWMEM`, jump straight to the slow path so every guest access
  reaches the C memory hooks (GBARecomp owns the bus, MMIO, the wait table and
  the prefetch model; the JIT's region fast paths would read mGBA's own arrays
  and charge mGBA's wait constants).
- `src/arm/jit/jit.c` `ARMJITSource`: under `SMA2_JIT_ROM_ONLY`, refuse to
  translate BIOS and mutable guest RAM (GBARecomp's DMA writes bypass the JIT's
  store-invalidation path).

Plus the mGBA header closure computed with the SH-4 toolchain (headers only; no mGBA frontend, video, audio, UI or platform code).
