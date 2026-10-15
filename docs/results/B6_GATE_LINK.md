# B6 — Full-corpus SH-4 gate link (measured linked sections)

Scope: link the full 64-shard SMA2 corpus with the real production dispatch path and the real
GBA subsystem, and measure the linked result. Host-side link only; no Dreamcast hardware was
touched.

## 1. Verdict

- **Undefined symbols at link: 0.** The full corpus, the full dispatch table, the production
  dispatch/interpreter bridge, and the full GBA subsystem link together.
- **Linked loadable size: 47,159,468 B (44.97 MiB)** — about **2.81×** retail 16 MiB main RAM.
  **NO-GO for the full static corpus on retail hardware**, now from a real link rather than a
  compile-stage estimate.
- The last remaining compile blocker was `gba_ppu.cpp`. It compiles cleanly on this toolchain
  with the two already-documented portability changes; both were applied only in a scratch
  copy. Production GBARecomp is untouched.

## 2. Inputs

| Item | Value |
|---|---|
| Corpus objects | `/tmp/sma2-sh4-all64-kosflags-20261015/obj/` (64, KOS target flags) |
| Corpus `.text` from per-function sections | 38,343,560 B |
| Dispatch table source | `/tmp/SMA2-PC-Trial/generated/dispatch_table.cpp`, sha256 `898c58a302031892b52a90352a528116af1fc719f689b159b2838fc299565e99`, 302,344 rows |
| Runtime objects | `/tmp/sma2-d3/obj_runtime/` (production dispatch, bridge, interpreter) |
| GBA subsystem objects | `/tmp/sma2-d1/obj_gba/` (28 TUs) |
| Toolchain | `sh-elf-g++` GCC 17.0.0 20260703 (experimental), GNU ld 2.47.20260726 |
| KOS | `KOS_ARCH=dreamcast`, `KOS_SUBARCH=pristine` |
| GBARecomp revision | `5e89943528aee7bc1c399bdbd035364a4b41dd10` (unmodified) |

Compile flags: `KOS_CFLAGS` + `-std=c++20 -Wall -fexceptions` +
`-include /tmp/sma2-d1/compat/glibcxx_c99_shim.h`. `-fexceptions` is required for the two
throw sites recorded in Gate D; its measured cost is the `.eh_frame` section below.

## 3. Link result

| Section | Bytes |
|---|---|
| `.text` | 40,991,288 |
| `.init` | 54 |
| `.fini` | 42 |
| `.rodata` | 3,664,920 |
| `.eh_frame` | 2,145,952 |
| `.gcc_except_table` | 276 |
| `.ctors` | 48 |
| `.dtors` | 8 |
| `.data` | 5,740 |
| `.got` | 12 |
| `.bss` | 351,124 |
| **PT_LOAD `p_memsz` sum** | **47,159,468** |

PT_LOAD segments: `0x8c010000` 40,991,288 B R E; `0x8e728000` 5,811,248 B R E;
`0x8ecb2cb0` 5,808 B file / 356,932 B mem R W. Sum 47,159,468 B. Entry `0x8c010000`.

Breakdown of `.text`: 38,343,560 B is the corpus (per-function sections); **2,647,728 B** is
the runtime, the full GBA subsystem, and KOS. That 2,647,728 B is the first measured cost of
the full GBA subsystem in a corpus link — Gate D recorded it as unmeasured.

Dispatch table in `.rodata`: `.rodata.kDispatchTable` 3,628,128 B (302,344 entries × 12 B)
plus `kDispatchTableLen` 4 B. In the object it also carries a 3,628,128 B `.rela` block; in
the static link those relocations resolve, so the runtime footprint is the 3,628,128 B.

## 4. Blockers closed, and how

1. **`gba_ppu.cpp` — measured, reproduced.** On unmodified source it fails
   `static_assert(sizeof(GbaForeignObjFocusTransform) == 72)` with `68 == 72`, because
   `alignof(uint64_t) == 4` on SH-4. With `alignas(8)` on the two `uint64_t` fields
   (`foreign_obj_focus.h:90-91`) it compiles.
2. **`std::min` mixed-type deduction** at `gba_ppu.cpp:514-516` (`uint32_t` is `unsigned long`
   on SH-4). Explicit `std::min<uint32_t>` casts compile cleanly.
3. **Include-path note:** a quoted include from `foreign_presentation_internal.h` resolves to
   the header next to the including file, so a patched header in a separate `-I` directory is
   bypassed. The patched copy must sit in the same directory as the including header. This is
   why the earlier patched-include attempt did not take effect.
4. **Overlay/JIT symbols** (`overlay_try_dispatch`, `gbarecomp::overlay_request_compile`) come
   from `overlay_loader.cpp` and `mod_function_hooks.cpp`; they were already in the Gate D
   object set.
5. **BIOS dispatch symbols** (`kBiosDispatchTable`, `kBiosDispatchTableLen`) come from the real
   generated BIOS artifact, not a stub.
6. **Duplicate scratch objects** in `/tmp/sma2-d3/obj_runtime/` (`main_d3.o`,
   `main_d3_bounded.o`, `cart_dispatch_table.o`, `recompiled_000.o`, `recompiled_001.o`,
   `runtime_arm_bounded.o`) caused multiple-definition errors and were excluded.

## 5. What this link is not

- It is a **link measurement**, not a runnable game image. The harness (`/tmp/sma2-b6/main_b6.cpp`)
  prints the table length and calls `runtime_dispatch(0x08000000)`; it does not construct a
  bus, video, audio, save, or input subsystem at runtime.
- No `--gc-sections` was used, so nothing was trimmed. That is deliberate: the point is the
  full-corpus footprint.
- No hardware load, no peak heap, no stack high-water, no VRAM accounting.
- `.eh_frame` at 2,145,952 B is a measured consequence of `-fexceptions`. Whether it can be
  removed is an open question, not a measurement.

## 6. Artifacts

| Artifact | Path | SHA-256 |
|---|---|---|
| ELF | `/tmp/sma2-b6/b6_gate.elf` | `d4ebfbf361e2360ad401ac12887a9a08813ab203f684f4e4792160538d28c11d` |
| Map | `/tmp/sma2-b6/b6_gate.map` | — |
| Link log | `/tmp/sma2-b6/log/link6.log` | 0 undefined references |
| Patched `gba_ppu.cpp` compile log | `/tmp/sma2-b6/log/gba_ppu.log` | exit 0, 0 errors |
| Harness source | `/tmp/sma2-b6/main_b6.cpp` | scratch |
| Patched headers/sources | `/tmp/sma2-b6/patched/src/gba/` | scratch, not upstream |

Link command (exact):

```text
kos-c++ -Wl,-Map=b6_gate.map -o b6_gate.elf obj/*.o obj_rt/*.o \
  /tmp/sma2-d1/obj_gba/*.o obj_gba_ppu/gba_ppu.o
```

## 7. Handoff

- Full static corpus remains **NO-GO** on 16 MiB, now backed by a real linked measurement
  (44.97 MiB loadable) rather than a compile-stage estimate.
- The two `gba_ppu.cpp` portability changes are the smallest upstream-able set for this
  blocker; they remain unapplied to production.
- The measured hot sets from [B7](B7_BASELINE_COVERAGE.md) are run-dependent: Run A needs
  172,996 B for 90% and 553,484 B for 95%; Run B needs 328,520 B for 90% and 872,340 B for
  95%. Adding any of those sizes to the measured 2,647,728 B runtime `.text` component is
  arithmetically inside 16 MiB. That is a projection, not a linked artifact, and it still needs
  a fallback path for the unexecuted 85–88% of the corpus plus its own table, ROM residency,
  and hardware peak measurement.
