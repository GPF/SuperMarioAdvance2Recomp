# SMA2 Dreamcast — Direct C++ / SDL2 Port Feasibility

**Research assignment. No implementation was started. No source was modified.**

Evidence labels: **MEASURED** (a command produced the number), **INFERRED**
(derived from measured data), **UNKNOWN** (not yet established).

---

## 1. Executive verdict

**CONDITIONAL GO — with a hard memory NO-GO for the full-game static corpus.**

| Question | Verdict | Basis |
|---|---|---|
| Can GBARecomp-generated C++ compile for SH-4? | **GO** | Two probes + all 64 generated shards compile successfully with `sh-elf-g++` using KOS SH-4 target flags (MEASURED; warnings recorded, §5). |
| Can it link as a KOS executable? | **GO** | `probe2.elf` links, 0 undefined symbols, all `R_SH_DIR32` (MEASURED). |
| Does the runtime compile unchanged for SH-4? | **NO — small, enumerable blockers** | 14 independent error sites in `runtime.cpp` + 8 in `runtime_arm.cpp` (23 diagnostics including one cascade; MEASURED, §3). |
| Is the SDL2 host API surface covered by the Dreamcast SDL2 port? | **Mostly GO** | Capability matrix, §2. |
| Does the full-game generated corpus fit in 16 MiB? | **NO-GO** | B6 measured **47,159,468 B (44.97 MiB)** of PT_LOAD memory in a full-corpus SH-4 link, about 2.81× retail main RAM. This is link memory, not peak runtime RAM. |
| Does the isolated two-function fixture execute correctly on SH-4? | **GO** | Physical Dreamcast run matched all 16 registers, CPSR, dispatch, and return trace (§7). Full runtime/game behavior remains unverified. |

Practical reading: the direct-C++ path is architecturally sound and appears to be the
cheaper route, but the compile-stage footprint is a **strong NO-GO signal for the full
static corpus** on retail hardware. The B6 link now measures the full-corpus linked image; peak
runtime RAM remains unmeasured. A
viable first target likely needs a *reduced corpus* plus the interpreter fallback. A custom
SH-4 backend is not required by any observed codegen defect; whether it helps the memory
budget needs measurement.

---

## 2. SDL2 vs SDL3 Dreamcast capability and compatibility matrix

### Which one SMA2 actually uses today

**SDL2.** `find_package(SDL2 REQUIRED)` and `SDL2::SDL2` in SMA2 `CMakeLists.txt:78-79`;
GBARecomp uses `#include <SDL.h>` (`src/runtime/host_window.cpp:29`) and SDL2-shaped APIs
throughout (`SDL_GameController*`, `SDL_RenderCopy`, `SDL_QueueAudio`, `SDL_KEYDOWN`).
SMA2's own `src/` contains **zero** SDL calls — all presentation is inside GBARecomp.

### Installed artifacts (MEASURED)

| Item | Path | State |
|---|---|---|
| SDL2 headers | `/opt/toolchains/dc/kos/addons/include/SDL2` | 82 headers, `SDL_config.h` present |
| SDL2 libs | `addons/lib/dreamcast/libSDL2.a`, `libSDL2main.a`, `libSDL2_test.a`, `libSDL2_image/ttf/net` | installed |
| SDL3 headers | `addons/include/SDL3` | 87 headers, **no `SDL_config.h` installed** |
| SDL3 libs | `libSDL3.a`, `libSDL3_test.a`, `libSDL3_net.a` | installed |
| Threads | `libpthread.a` in addons lib | required by both ports |

### Capability matrix

| Capability | Dreamcast SDL2 (repo `/home/gpf/code/dreamcast/SDL2`) | Dreamcast SDL3 (repo `/home/gpf/code/dreamcast/SDL3`) | SMA2/GBARecomp requirement |
|---|---|---|---|
| Window creation | `SDL_dreamcastwindow.c`; video mode via `SDL_HINT_DC_VIDEO_MODE` (320x240/640x480/PAL/NTSC) | Same driver family + `SDL_dreamcastkeyboard.c`, `SDL_dreamcastmouse.c` | `SDL_CreateWindow`, `SDL_SetWindowFullscreen/Size/Position/Title` |
| Renderer | Software framebuffer (`SDL_dreamcastframebuffer.c`), OpenGL via **GLdc**, native PVR (`SDL_render_dreamcast_pvr.c`) | Software framebuffer, OpenGL via GLdc, native PVR | `SDL_CreateRenderer`, `SDL_SetRenderTarget`, `SDL_RenderSetLogicalSize` |
| Texture formats | PVR path: **RGB565, ARGB1555, ARGB4444** only (`SDL_render_dreamcast_pvr.c:47-49`); framebuffer accepts RGB565/ARGB1555 (`SDL_dreamcastframebuffer.c:245-246`); software path accepts full SDL2 set | Same PVR format set | `SDL_CreateTexture` + `SDL_UpdateTexture` — GBA PPU output is 15/16-bit BGR, compatible |
| Input | Maple joystick (`src/joystick/dreamcast`), haptics (Purupuru), sensors; **Dreamcast GUID is in SDL2's built-in game-controller DB** (`SDL_gamecontrollerdb.h:1005`) → `SDL_IsGameController` succeeds | Gamepad API (renamed); joystick driver present | `SDL_GameControllerOpen/GetAxis/GetButton`, `SDL_GetKeyboardState`, `SDL_PollEvent` |
| Audio | AICA `snd_stream` with `PlayDevice` (`SDL_dreamcastaudio.c:215,543`) → core `SDL_QueueAudio` path works | AICA driver present | `SDL_OpenAudioDevice`, `SDL_QueueAudio`, `SDL_GetQueuedAudioSize`, `SDL_ClearQueuedAudio` |
| Timing | `SDL_TIMER_UNIX` enabled in the installed SDL2 config; `src/timer/dreamcast/SDL_systimer.c` | `src/timer/dreamcast/SDL_systimer.c` | `SDL_GetTicks`, `SDL_GetPerformanceCounter/Frequency` (FramePacer) |
| Threads/sync | `src/thread/dreamcast` (mutex/sem/cond/thread) + `libpthread` | No `src/thread/dreamcast`; relies on generic pthread | `SDL_CreateMutex/Lock/Unlock`; runtime uses `std::mutex`, `std::condition_variable`, `thread_local` |
| Filesystem | `src/filesystem/dreamcast/SDL_sysfilesystem.c`, `src/file/dreamcast/SDL_rwops.c` | `src/filesystem/dreamcast` | `std::filesystem` + `read_file()` — KOS has no desktop FS; needs romdisk/dcload or a KOS adapter |
| OpenGL | GLdc (`-lGL`), tested (`nehe06`, `fbo_*`, `glanimation`) | GLdc, tested (`nehe06`, `3d`) | Only the OpenGL renderer path; avoidable if the PVR/software renderer is used |
| Maturity | SDL 2.33.0 fork; hardware logs present (`hardware-alpha-extended-fixed.log`, `flycast.log`) | SDL 3.5 fork; **documented hardware passes** (`test/dreamcast/TESTING.md`, `results/phase0-baseline.md` Gates 1–9 hardware PASS on `192.168.0.128`) | — |

### Recommendation

**Use SDL2 for the first Dreamcast target.**

Reasons:
1. GBARecomp's presentation layer is SDL2-shaped; SDL3 requires renaming
   (`SDL_RenderCopy`→`SDL_RenderTexture`, `SDL_GameController*`→`SDL_Gamepad*`,
   `SDL_KEYDOWN`→`SDL_EVENT_KEY_DOWN`, `SDL_HapticOpen` is gone in SDL3 — only
   `SDL_HapticEffectSupported`/`SDL_HapticRumbleSupported` remain in the installed SDL3 header).
2. The Dreamcast GUID is already in SDL2's controller DB, so SMA2's
   `open_game_controller()` path (`host_window.cpp:544-549`) works without a mapping patch.
3. SDL2's installed `SDL_config.h` is complete; SDL3's is not installed.

Take SDL3 only if the PVR renderer's hardware-validated behavior becomes the deciding
factor — SDL3's PVR work is better documented and hardware-passed. That is a renderer
quality argument, not an API-compatibility one.

---

## 3. C++ runtime / toolchain dependency analysis

### Toolchain (recaptured, same as probe 2)

`sh-elf-g++` GCC 17.0.0, binutils 2.47, KOS 2.3.0, `KOS_ARCH=dreamcast`,
`KOS_SUBARCH=pristine`. `-std=c++20` is accepted.

### What compiles unchanged for SH-4 (MEASURED)

| Component | Status |
|---|---|
| Generated game shards (`recompiled_*.cpp`) | **Compile clean** — 7 shards compiled, exit 0, warnings only |
| `dispatch_table.cpp` | **Compiles clean** |
| `symbol_lookup.cpp` | **Compiles clean** |
| `interpreter.cpp` (fallback) | **Compiles clean**, 8,796 bytes code |
| `<filesystem>` header | **Compiles** (link behavior untested) |

### What fails for SH-4 (MEASURED, exact errors)

`runtime_arm.cpp` (8 error diagnostics):
- `std::strtoull` not a member of `std` (lines 213, 223)
- `std::snprintf` not a member of `std` (lines 233, 237, 324, 334, 915)
- exception handling required (line 878) under `-fno-exceptions`

`runtime.cpp` (15 error diagnostics, 14 independent + 1 cascade):
- `std::max(unsigned int, uint32_t&)` template deduction failure (lines 91, 93, 127 in
  `view_config.h`) — `uint32_t` is `long unsigned int` on SH, so deduction fails
- `std::strtoull` (461, 475, 2641, 3349)
- `std::snprintf` (1212, 1217, 2570, 2925, 3469, 4220)
- exception handling required (1496)
- `‘e’ was not declared in this scope` (1517) — **cascade** of the disabled exception
  handler at 1496, not an independent defect

All are **portability nits, not architecture blockers** — but they belong in GBARecomp,
not in SMA2.

### Desktop-only components

| Component | Why desktop-only |
|---|---|
| `overlay_compile.cpp` | `std::system()` child process + `dlopen`/`dlsym` (lines 218, 231) — runtime GCC compile of self-healed functions. No equivalent on KOS. |
| `host_window.cpp` | SDL window/renderer/input; needs a KOS presentation adapter |
| `mobile_platform.cpp`, `asset_picker.cpp`, launcher/recomp-ui | Desktop UI, config.ini, keybinds |
| `multiplayer_*.cpp`, `link_hub`, `gba_wireless`, `gba_gyro` | Network/sensor subsystems; exclude from an initial target |
| `cosim.cpp` | Debug/oracle, host-only |

### Structural blocker

`gbarecomp_runtime` is **one static library** that mixes core runtime (bus, PPU, audio,
save, dispatch) with the desktop frontend (`host_window.cpp`, `mobile_platform.cpp`,
`asset_picker.cpp`, `overlay_compile.cpp`, `multiplayer_*`). A KOS build cannot select
the core without the frontend unless GBARecomp splits the target or adds a KOS platform
adapter behind the existing `host_platform.h` / `launcher_seam.h` seam.

### Positive architectural facts for the AOT path

- **No executable memory, no self-modifying code** in the generated path → no icache
  flush, no W^X problem, no JIT allocator. This is a genuine advantage over a dynarec on
  real hardware.
- Static link, no dynamic loader, no `dlopen` needed for the generated corpus.
- PIC is enabled for `gbarecomp_armv4t`; the probe ELF resolved `.got` (12 bytes) fine.
- `thread_local` (`g_runtime_tail_arg`) is served by KOS TLS (`tls_static.c`), which is
  exactly what the probe2 reproduction failure exposed.

---

## 4. Proposed Dreamcast executable architecture

```
KOS arch startup (_start, 0x8c010000)
  └─ KOS main()
       ├─ KOS platform adapter (KOS-owned, in GBARecomp)
       │    ├─ video: SDL2 window + PVR or software framebuffer (240x160 GBA mode)
       │    ├─ input: maple controller via SDL2 game-controller path
       │    ├─ audio: AICA snd_stream, SDL2 queue path
       │    ├─ timing: KOS timer / SDL_TIMER_UNIX
       │    └─ storage: romdisk or dcload-mounted ROM + save buffer
       ├─ GBARecomp core runtime (bus, PPU, DMA, IRQ, save, dispatch)
       ├─ Generated corpus (reduced, selectively linked) + dispatch table
       ├─ Interpreter fallback for uncovered/mutable code (8.8 KB, measured)
       └─ Excluded: launcher UI, mods, network, wireless, gyro, overlay JIT-compile
```

Two variants:

- **A. Headless KOS executable** — no SDL, no PVR. Uses `run_game()` with `--steps`/
  `--frames`/`--dump-bmp` (headless path already exists in `runtime.cpp:1448`). Useful
  for correctness gates only; still needs the runtime split.
- **B. Minimal SDL2 KOS executable** — window + one renderer + controller + audio, no
  launcher/UI/mods. This is the first testable game-shaped artifact.

Both require the GBARecomp target split described in §3. Neither is constructible today
without a GBARecomp change.

---

## 5. Memory and code-size risks (with evidence)

### Measured

| Item | Value | Method |
|---|---|---|
| Generated functions (SMA2 corpus) | **23,042** | `grep -c "^void gf_"` over `/tmp/SMA2-PC-Trial/generated` |
| Dispatch table entries | **302,344** | `grep -c "^    {0x"` |
| Dispatch table SH-4 size | **3,628,132 bytes** | `sh-elf-g++` compile of `dispatch_table.cpp`, `.rodata` section size |
| ROM | **4,194,304 bytes** | `stat` on `super_mario_world_usa.gba` |
| SH-4 code per shard (7-shard sample, shards 0/10/20/30/40/50/60) | 667,800 / 588,724 / 564,904 / 595,984 / 659,532 / 548,956 / 541,696 bytes | `sh-elf-objdump -h` / `sh-elf-size` per object |
| Sample total | **4,167,596 bytes for 2,584 functions** → **1,612.8 bytes/function** | MEASURED (re-verified against the objects) |
| All 64 generated shards | **38,343,560 bytes `.text` (36.57 MiB)**; 64/64 succeeded, with 128,401 compiler warnings | MEASURED using KOS SH-4 flags; commands, status, sizes, objects, and logs under `/tmp/sma2-sh4-all64-kosflags-20261015/` |
| Interpreter fallback | **8,796 bytes** | `sh-elf-size` |
| KOS static link floor | ~178,924 bytes | probe2 report |
| Host Linux baseline | `.text` 43.3 MB, `.rodata` 2.55 MB, `.data.rel.ro` 5.29 MB, `.bss` 1.29 MB | `size` on the PC exe — **not** an SH-4 estimate |

### Full-corpus compile-stage accounting

The valid all-64-shard SH-4 compile directly measured generated code; this replaces the
seven-shard extrapolation for this corpus. The former sample extrapolation (37,163,215
bytes) was about 3.1% below the measured total. An earlier all-64 attempt omitted KOS
target flags and is invalid: its ELF flags were `0x16` (SH2A/SH3 family), not SH-4. That
run remains preserved separately under `/tmp/sma2-sh4-all64-20261015/`.

```
38,343,560 bytes (36.57 MiB) generated `.text` — 64 SH-4 objects, compile only
+ 3,628,132 bytes (3.46 MiB) dispatch table
+ 4,194,304 bytes (4.00 MiB) ROM (if resident)
+ 178,924 bytes (0.17 MiB) KOS link floor
= 46,344,920 bytes (44.20 MiB) compile-stage budget estimate before runtime and buffers
+ runtime (bus/PPU/audio/save/dispatch, unmeasured)
+ KOS/SDL video + audio buffers + stacks + allocator overhead
```

This estimate is about **2.76× the 16 MiB retail main-RAM budget** before runtime and
buffers. It combines pre-link object `.text`, table `.rodata`, ROM residency, and the KOS
`.text` link floor. Objects were built with `-ffat-lto-objects`; a final LTO link may
change code size. This is not a linked full-game ELF or a peak-RAM measurement.

### Why `--gc-sections` does not rescue it

The full dispatch table contains a function pointer for every generated function: its
source has 23,042 unique generated-function references, matching the 23,042 generated
functions. Therefore every `.text.gf_*` section is live from the linker's point of view,
and section garbage collection cannot drop unexercised code while that full table is
linked. This claim applies to the emitted full table. A reduced-corpus build changes both
the code corpus and dispatch table, so its size must be measured separately; the 44.20
MiB full-corpus compile-stage estimate is not a ceiling on a selective build.

### Additional memory facts

- The runtime loads the whole ROM into a `std::vector<unsigned char>`
  (`host_platform.h read_file()`), so ROM residency is a real 4 MB cost unless a
  streaming/romdisk policy is chosen.
- SMA2's two `code_copy` hints (0x360, 0x800) do not bound mutable code; the interpreter
  fallback must remain available for those regions.
- Dreamcast PVR texture memory is a separate 8 MiB VRAM pool, not part of the 16 MiB
  main-RAM budget. Track texture/TA allocations as a separate hardware limit; see the
  [KallistiOS memory diagram](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/asic.c).

### All-64 compile gate — COMPLETE

All 64 generated translation units compiled successfully for SH-4; 128,401 compiler
warnings were recorded. Four workers completed the run in 405.8 seconds. `sh-elf-size`
summed object `.text` to 38,343,560 bytes; the objects contain no `.data` or `.bss`.
This was a compile-only measurement at the time: no link or hardware execution. The B6
follow-up linked the full corpus and measured 47,159,468 B of PT_LOAD memory; it did not
measure peak runtime RAM or hardware execution.

The exact source set was `/tmp/SMA2-PC-Trial/generated` and the GBARecomp checkout was
`/home/gpf/code/gbarecomp` at `5e89943528aee7bc1c399bdbd035364a4b41dd10`. The generated
files' original generator revision was not recorded; this compile used the listed
GBARecomp checkout for headers. The compiler was `sh-elf-g++ (GCC) 17.0.0 20260703
(experimental)` with GNU Binutils `2.47.20260726` (`sh-elf-size`).
Compile flags came from `/opt/toolchains/dc/kos/environ.sh` (`KOS_CFLAGS`, including
`-O3 -flto=auto -ffat-lto-objects -m4-single -ml -mfsrra -mfsca`, section flags, and
KOS system include paths), plus `-fno-exceptions -fno-rtti -std=c++20` and generated /
GBARecomp `src/armv4t` and `src/runtime` include paths. Sample object headers report
ELF32 little-endian SuperH, flags `0x0c` (SH4A).

Correctly targeted artifacts are under `/tmp/sma2-sh4-all64-kosflags-20261015/`:
`compile_all.py`, `metadata.txt`, `all_compile_commands.txt`, `compile_status.tsv`,
`object_sizes.txt`, 64 object files, and one log per shard. The invalid default-target run
is preserved under `/tmp/sma2-sh4-all64-20261015/`. The seven-shard sample remains a
cross-check, but its command/log provenance is partial.

**Reproducibility gap in this report's own sample:** the seven objects exist, but the
seven compile commands and per-shard logs were not preserved. The flags used were:

```
sh-elf-g++ -c -O2 -std=c++20 -fno-exceptions -fno-rtti \
  -ffunction-sections -fdata-sections \
  -I /tmp/SMA2-PC-Trial/generated \
  -I /home/gpf/code/gbarecomp/src/armv4t \
  -I /home/gpf/code/gbarecomp/src/runtime <shard> -o <obj>
```

Next memory measurement: link a *reduced corpus* (e.g. functions reachable from the
entry graph) with its matching reduced dispatch table and `--gc-sections`, then inspect
the resulting loadable segments.

---

## 6. Guest execution correctness gate — COMPLETE FOR THE ISOLATED FIXTURE

The two-function behavioral gate in `NEXT_TASK.md` passed on physical Dreamcast hardware.
The harness executed the generated functions through the generated dispatch table and
matched the complete register/CPSR state and control-flow trace. See the
[hardware result](results/SH4_TWO_FUNCTION_HARDWARE.md) for the exact ELF and console log.

Expected final state (hand-derived before execution, then observed on hardware):

```
R[1]=0x00000011  R[2]=0x00000022  R[3]=0x00000033
R[12]=0x08000140 R[14]=0x08000140 R[15]=0x08000140  CPSR_T=0
```

The hardware trace confirms this fixture takes the dispatch, generated A-to-B call,
callee return to `0x080000CC`, and sentinel exit paths. This confirms the bounded fixture
contract runs; it does not validate the production runtime or arbitrary call depth.

One ABI observation worth carrying forward: guest `BX LR` is implemented as a **host C
return contingent on the harness call-return stack**, not on guest `LR` alone. On real
hardware that couples guest call depth to host stack depth. With 23k functions and
recursive guest calls, host stack depth is an unmeasured risk. This is a design property
of the AOT path, not a defect — but it must be stated before any full-game claim.

---

## 7. Physical hardware validation — COMPLETE FOR THE TWO-FUNCTION FIXTURE

The reproducibility gap was closed for a new harness build using KOS `Makefile.rules`,
`kos-c++`, and `kos-cc`; the original `probe2.elf` was preserved. The test ran twice
successfully on physical Dreamcast hardware under mGBADC's shared console lock. Both
runs matched all 16 registers and CPSR, observed the expected generated function entries,
BL/BX LR path, dispatch hit, and sentinel return, and ended with `Program returned 0`.

Full provenance, expected/actual state, trace, lock owner, ELF checksum, and complete
console output location are recorded in the [hardware result](results/SH4_TWO_FUNCTION_HARDWARE.md).
This validates only the fixture and its explicit harness hooks. Production runtime,
SMA2 integration, and game behavior remain unverified.

---

## 8. Smallest viable C++/SDL2 KOS build experiment

Proposed production/runtime experiment; no production runtime or SDL2 game build has been executed. The separate KOS fixture harness is documented in §7:

1. **All-64 compile memory gate — COMPLETE.** Generated `.text` is 38,343,560 bytes
   using KOS SH-4 target flags. The isolated behavioral gate has since passed separately;
   this compile did not replace that execution evidence. Commands and logs are under `/tmp`.
2. **Fix the 22 independent error sites** (14 in `runtime.cpp`, 8 in
   `runtime_arm.cpp`; 23 compiler diagnostics total because one `runtime.cpp` diagnostic
   is a cascade). Address qualified `strtoull`/`snprintf`, `std::max` type mismatch,
   and exception-gated paths. Small,
   revertible, belongs in GBARecomp.
3. **Split the runtime target** in GBARecomp: core runtime vs host presentation, with a
   KOS adapter implementing `host_platform.h` (file I/O, time, threads) and a minimal
   SDL2 presentation adapter.
4. **Build a KOS target** with `Makefile.rules`, linking: core runtime + one generated
   shard + dispatch table + symbol map + `libSDL2.a` + `libpthread` + `libGL` (only if
   the OpenGL renderer is chosen; otherwise omit GL entirely).
5. **Run headless first** (`--steps`/`--frames`/`--dump-bmp` path), then add window +
   controller + audio.

Explicitly excluded from the first target: launcher/recomp-ui, mods, network/multiplayer,
link hub, wireless, gyro, cosim, overlay compile (runtime GCC), and any JIT.

---

## 9. Repository ownership

| Proposed change | Owner |
|---|---|
| `strtoull`/`snprintf` qualification, `std::max` type fix, exception gating | **GBARecomp** |
| Runtime target split (core vs presentation), KOS platform adapter | **GBARecomp** |
| Reduced-corpus / selective-link policy, dispatch-table storage policy | **GBARecomp** (shared), with SMA2 supplying coverage config |
| ROM residency / streaming policy | **GBARecomp** (shared runtime), SMA2 sets title policy |
| Title startup, `game.toml`, ROM identity, code-copy hints | **SMA2** |
| Hardware validation records, memory budget measurements | **SMA2** |
| SDL2 vs SDL3 choice | **SMA2** decision, executed against the existing SDL ports (no changes to them) |
| SH-4 backend work | **GBARecomp**, only if the memory gate forces it |

No code is copied from mGBADC, gpSP, or brucemGBA. Neither reference repo was modified.

---

## 10. Recommended implementation sequence for Pi

The two-function execution and KOS-link reproducibility gates are complete (§7).
`NEXT_TASK.md` now governs the reduced-corpus measurement as the next bounded experiment.
After its memory/coverage result, any runtime work remains in GBARecomp and should proceed
through small portability fixes, a target split/platform adapter, then a minimal headless
SDL2 build. Consider a SH-4 backend only if measured reduced-corpus results justify it.

---

## 11. Reference comparison (read-only)

`/home/gpf/code/dreamcast/gpsp-sh4-jit` and `/home/gpf/code/dreamcast/brucemGBA` return
through a JIT-managed return path rather than the host C call stack. The AOT path under
test couples guest call semantics to the host ABI (`pr` + call-return stack). That is the
main architectural difference relevant here. It is also the reason a dynarec has a
smaller memory footprint: it emits code on demand instead of carrying 23k precompiled
functions. No code was copied; neither repo was modified.

---

## 12. Correction record

The first cut of this report mis-added the 7-shard sample (3,607,596 instead of
4,167,596) and miscounted its functions (2,488 instead of 2,584), understating the
per-function cost as 1,450 instead of 1,612.8 bytes. The exact unrounded sample
extrapolation is 37,163,215 bytes of code; the first all-64 attempt reported
38,680,500 bytes, but that run omitted the KOS SH-4 target flags and is invalid. Its
46,681,860-byte (44.52 MiB) accounting is likewise invalid as an SH-4 estimate. The
report also first stated a 43.07 MiB extrapolated total; that was based on the sample
and has been superseded by the correctly targeted compile below. It also reported 16 `runtime.cpp`
diagnostics where the log contains 15, one of which (`‘e’ was not declared in this
scope`, line 1517) is a cascade of the disabled exception handler at line 1496. The
compile logs contain 23 diagnostics overall, representing 22 independent error sites.
The first all-64 attempt omitted KOS target flags (object ELF flags `0x16`) and its size
result was invalid for SH-4. A corrected run using KOS `KOS_CFLAGS` compiled all 64
shards successfully: generated `.text` totals 38,343,560 bytes (36.57 MiB), with 128,401
compiler warnings. Adding the table, resident ROM, and KOS link floor gives a 46,344,920
byte (44.20 MiB) compile-stage estimate, about 2.76× retail RAM. The later B6 link measured
47,159,468 B of PT_LOAD memory; peak RAM remains unmeasured. Dreamcast VRAM is a separate 8 MiB pool, not part
of the 16 MiB main-RAM budget (KOS [ASIC memory map](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/asic.c)).

---

## 13. What this report does **not** claim

- No production-runtime, SMA2, or game state has been executed. The two-function fixture
  gate passed; this does not establish correctness beyond that fixture.
- No full SMA2 Dreamcast hardware support is claimed; only the isolated two-function
  fixture has been executed on hardware.
- The 7-shard sample is a measurement of seven objects; the all-64 object `.text` sum is
  also measured. The combined full-corpus memory figure adds ROM/table/KOS floors but is
  not a linked ELF or peak-RAM measurement.
- The SDL capability matrix is derived from source and installed configs, not from a
  SMA2-shaped SDL program running on hardware.
- The GBARecomp checkout used for the probes (`5e89943528aee7bc1c399bdbd035364a4b41dd10`)
  is not SMA2's pinned gitlink (`647e2ab8c69666a55d04d82437002f1f1772c8bf`); no
  equivalence is assumed.
