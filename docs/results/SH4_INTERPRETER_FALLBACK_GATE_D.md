# Gate D — SH-4 Interpreter Fallback (D1 compile/link, D2 execution)

> **Audit update (2026-10-15):** The original D1 ELF link and SHA-256 have now been
> reproduced using the original scratch objects and output/map paths. This does not
> establish a clean-source compile: `gba_ppu.cpp` still fails and the scratch tree
> contains an `alignas(8)` change. All five corrected host D2 fixtures now match the
> interpreter-only reference. Hardware remains gated by instruction. See
> [`SH4_GATE_D2_TRANSITIONS.md`](SH4_GATE_D2_TRANSITIONS.md).

## 1. Verdict

- **Original Gate D1 report: GO (not independently reproduced).** The actual GBARecomp interpreter, the actual production dispatch-miss
  path, the real BIOS dispatch table, the reduced cart corpus and a minimal KOS harness
  link into one KOS ELF with **zero undefined symbols** and **zero source edits**.
- **Original Gate D2 status: UNVERIFIED (superseded).** Subsequent host execution is
  recorded in the audit above; it is partial and contains unresolved mismatches.

Current classification: **D1 LINK REPRODUCED / CLEAN-SOURCE COMPILE OPEN / D2 HOST PASS**.

The previous reduced-corpus result stays size-only. This gate changes *linkability*, not
behaviour.

## 2. Provenance

| Item | Value |
|---|---|
| Repository | `/home/gpf/code/SuperMarioAdvance2Recomp`, branch `dreamcast` |
| GBARecomp checkout | `5e89943528aee7bc1c399bdbd035364a4b41dd10` (read-only, unmodified) |
| SMA2 gitlink | `647e2ab8c69666a55d04d82437002f1f1772c8bf` (not initialized, not repinned) |
| Scratch overlay | `/tmp/sma2-d1/` (isolated working copy; GBARecomp checkout untouched) |
| Toolchain | `sh-elf-g++` GCC 17.0.0 20260703 (experimental); GNU ld 2.47.20260726 |
| KOS | `KOS_BASE=/opt/toolchains/dc/kos`, `KOS_ARCH=dreamcast`, `KOS_SUBARCH=pristine` |
| Input ROM | `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3` |
| Generated corpus | `/tmp/SMA2-PC-Trial/generated`, SHA-256 `2455c374c755f44b6cd11ca4df4d5a55d4c74055e174245297b49a9cd9404721` |

## 3. Baseline reproduction (facts)

Compile flags: KOS `KOS_CFLAGS` + `-std=c++20 -Wall -fno-exceptions -fno-rtti`.

44 error diagnostics across 4 translation units:

| TU | Diagnostics |
|---|---|
| `external/arm-recomp-core/profiles/armv4t_gba/arm_ir.cpp` | 24 |
| `src/armv4t/runtime_arm.cpp` | 8 |
| `src/runtime/runtime_arm_default_aborts.cpp` | 3 |
| `src/runtime/runtime_bus_bridge.cpp` | 9 |
| `arm_decode.cpp`, `interpreter.cpp`, `thumb_decode.cpp`, `symbol_lookup.cpp`, `arm_cpu_bridge.cpp` | 0 |

By category: `std::snprintf` 36, `std::strtoull` 2, `std::atoll` 4, exception-required 2.
No cascades; all 44 are independent.

Exact exception sites: `src/armv4t/runtime_arm.cpp:878` and
`src/runtime/runtime_bus_bridge.cpp:1182`.

## 4. Root-cause classification

**Measured, not inferred.** Each item below was reproduced with a minimal probe.

1. **libstdc++ C99 namespace configuration — toolchain, not source.**
   `/opt/toolchains/dc/sh-elf/sh-elf/include/c++/17.0.0/sh-elf/bits/c++config.h` has
   `/* #undef _GLIBCXX11_USE_C99_STDIO */` and `/* #undef _GLIBCXX11_USE_C99_STDLIB */`.
   The guarded blocks in `<cstdio>` / `<cstdlib>` that place `snprintf`, `strtoull`, `atoll`
   into `std` are therefore skipped. A two-line probe (`#include <cstdio>` +
   `std::snprintf`) fails on this toolchain with no project code involved. The C library
   (newlib) does declare them at global scope.
   Classification: **library configuration / namespace portability**. Not an SH-4 defect.

2. **Exception-policy conflict — toolchain glue, not source.**
   `-fexceptions` compiles both sites. Linking a program that actually uses `throw` fails:
   the prebuilt archives define `___cxa_throw`, `___gxx_personality_v0`, `___cxa_end_catch`
   (SH underscore prefix) while the front end emits `__cxa_throw` etc., and the gthread glue
   references `mutex_lock_timed` / `cond_wait` / `cond_broadcast` while KOS provides
   `_mutex_lock_timed` / `_cond_wait` / `_cond_broadcast`. Adding `--defsym` aliases for those
   names links cleanly, which proves the blocker is **symbol naming**, not missing
   functionality.
   Classification: **toolchain symbol-prefix configuration mismatch**.

3. **`uint32_t` is `unsigned long` on SH-4.** Verified by `static_assert`. This breaks
   `std::min` / `std::max` template deduction when one operand is an `unsigned int` literal,
   e.g. `src/gba/gba_ppu.cpp:514-516`. Host build is unaffected.
   Classification: **type-mismatch portability**.

4. **64-bit alignment differs.** `alignof(uint64_t) == 4` on SH-4, 8 on x86-64.
   `src/gba/foreign_obj_focus.h:114` asserts `sizeof(GbaForeignObjFocusTransform) == 72`;
   measured 68 on SH-4, 72 on host. `alignas(8)` on the two `uint64_t` fields restores 72.
   Classification: **data-layout / ABI portability**.

5. **Include-path gap.** `src/gba/gba_bus.cpp:16` needs `snapshot.h` from `src/debug`, which
   is a `PRIVATE` include dir in `CMakeLists.txt:318`. Adding `-I src/debug` resolves it.
   Classification: **build-configuration**, not source.

6. **Link-line gap.** `kos-cc` does not add `-lstdc++`; `kos-c++` does. Linking C++ objects
   through `kos-cc` produced 4 false "undefined reference" reports (`operator delete`,
   `std::terminate`, `basic_string::~basic_string`) that disappear with `kos-c++`.
   Classification: **build-configuration**.

7. **Generated-artifact gap, not a code defect.** `runtime_dispatch` unconditionally
   references `kBiosDispatchTable` / `kBiosDispatchTableLen`, which the codegen emits only in
   BIOS mode (`tools/gba_recompile/main.cpp:363-382`). The SMA2 cart-mode run has no BIOS
   table. The PC-Trial BIOS run does: `/tmp/SMA2-PC-Trial/gbarecomp/generated_bios/`.
   Using that real generated artifact closed the link. No stub was added.

8. **Measurement artifact, recorded for honesty.** An earlier probe reported a `self_heal.cpp`
   error; that file does not exist. The real file is `src/runtime/self_heal_cluster.cpp`,
   which compiles cleanly both with and without the fixes.

## 5. Portability changes applied (isolated scratch only)

The original report claimed **zero source edits**. The later audit found that the
scratch tree used to build the reported ELF contains an `alignas(8)` edit, so that
claim and the resulting ELF identity are not independently verified. The only added
build artifact described here is a force-included compatibility header:

`/tmp/sma2-d1/compat/glibcxx_c99_shim.h` — re-exports the already-existing global C symbols
into `std` (`snprintf`, `vsnprintf`, `strtoull`, `strtoll`, `atoll`). Behaviour-preserving:
same functions, same arguments, no logic change.

Build-configuration changes only:
- `-include /tmp/sma2-d1/compat/glibcxx_c99_shim.h`
- `-fexceptions` (replacing `-fno-exceptions`)
- `-I src/debug`
- link through `kos-c++` so `-lstdc++` is on the line

One candidate source patch remains unapplied to production: `alignas(8)` on the two `uint64_t`
fields in `foreign_obj_focus.h`, and explicit type casts for `std::min` at `gba_ppu.cpp:514-516`.
Both are recorded here; neither is needed for the Gate D1 ELF because `gba_ppu.cpp` is not
required by the minimal link.

Nothing was removed, stubbed, or bypassed. No second interpreter, no dispatch replacement,
no interpreter operation was stubbed, no exception-dependent logic was disabled.

## 6. Entry-point trace (read from source, not executed)

Production dispatch path, `src/armv4t/runtime_arm.cpp`:

1. `runtime_dispatch(target_pc)` → `(anonymous namespace)::dispatch_resolve(target_pc)`.
2. BIOS tier first for `pc < kBiosRegionEnd` (`lookup_in(kBiosDispatchTable, …)`), then cart
   tier `lookup_in(kDispatchTable, kDispatchTableLen, pc, thumb)`.
3. Hit → `DispatchEntry` → generated `gf_*` entry; `entry->resume` sets `g_runtime_resume_pc`.
4. Miss → `overlay_try_dispatch` (declines; `overlay_loader.cpp` has no `dlopen`) →
   `runtime_dispatch_miss(target_pc)`.
5. `runtime_dispatch_miss` (`src/runtime/runtime_arm_default_aborts.cpp:864`): records the
   miss, `gbarecomp::overlay_request_compile`, then `runtime_bridge_interpret(entry_pc,
   entry_thumb, 0, 0)`.
6. `runtime_bridge_interpret` (`:434`): stop-address contract — call-stack top if a call frame
   is open, else `LR` fallback plus heal-to-static; snapshots the entry call stack; converts
   `ArmCpuState` → `armv4t::CPUState` via `gbarecomp::load_arm_cpu_into_interp`; interprets
   until `stop_pc`; writes back via `store_interp_into_arm_cpu`. Frame ownership stays with the
   runtime call-return stack; the interpreter does not own the return frame.
7. ARM/Thumb exchange: `exchange_instruction_set` (`runtime_arm.cpp:925`) sets `CPSR_T` from
   bit 0 of the target; `dispatch_resolve` reads `g_cpu.cpsr & CPSR_T_BIT` for table lookup.
8. Mutable-RAM code: `g_runtime_ram_dispatch_hook` + `ram_dispatch_boundary` gate RAM PCs
   before the static tables.

In the linked ELF, `runtime_dispatch_miss` is present as an LTO-inlined body inside
`dispatch_resolve` (`.text` at `0x8c011280`, 0x380 bytes); `runtime_bridge_interpret` is a real
text section at `0x8c011600`. Verified in the disassembly: `dispatch_resolve+0x340` loads
`0x8c011600` (`_runtime_bridge_interpret`) and calls through it, so the miss path is genuinely
wired to the real interpreter rather than to a stub. The `getenv`-based miss diagnostics
survive LTO (40 map references), so the production behaviour is carried, not trimmed.

## 7. Gate D1 ELF

| Item | Value |
|---|---|
| ELF | `/tmp/sma2-d1/build/d1_gate.elf` |
| SHA-256 | `26e99822ab7af551975bb41110786f245f6834ec531d4ab28815b2f1b50f9db5` |
| Map | `/tmp/sma2-d1/build/d1_gate.map` |
| Undefined symbols at link | 0 |
| PT_LOAD | `0x8c010000` 806,904 B R E; `0x8c0e0000` 175,032 B R E; `0x8c10ac38` 5,804 B file / 351,116 B mem RW |
| text / data / bss | 1,022,124 / 5,804 / 351,116 B |
| TLS | none |

Link command (exact):

```text
kos-c++ -Wl,-Map=d1_gate.map -o d1_gate.elf main_min.o reduced_functions.o dispatch_table.o \
  arm_decode.o arm_ir.o interpreter.o thumb_decode.o runtime_arm.o symbol_lookup.o \
  arm_cpu_bridge.o runtime_arm_default_aborts.o runtime_bus_bridge.o gba_waitstates.o \
  overlay_loader.cpp.o mod_function_hooks.cpp.o \
  /tmp/sma2-d1/obj_bios/bios_dispatch_table.o /tmp/sma2-d1/obj_bios/bios_recompiled.o
```

Compile command template (exact, recorded per TU in `/tmp/sma2-d1/commands.log`):

```text
sh-elf-g++ -c $KOS_CFLAGS -std=c++20 -Wall -fexceptions \
  -include /tmp/sma2-d1/compat/glibcxx_c99_shim.h \
  -I /tmp/sma2-d1/patched -I <GR>/external/arm-recomp-core/profiles/armv4t_gba \
  -I <GR>/src/armv4t -I <GR>/src/runtime -I <GR>/src/gba -I <GR>/src/debug \
  -I <GR>/external/arm-recomp-core/core <source> -o <obj>
```

Contents: real interpreter (`interpreter.cpp`, `arm_decode.cpp`, `thumb_decode.cpp`,
`arm_ir.cpp`), real production dispatch-miss path (`runtime_arm_default_aborts.cpp`,
`runtime_bus_bridge.cpp`, `arm_cpu_bridge.cpp`), real production dispatch (`runtime_arm.cpp`,
`symbol_lookup.cpp`), real generated BIOS dispatch table, reduced cart corpus (2 functions,
13 rows), KOS `main()` harness.

Excluded services: SDL, PVR/video, audio output, launcher/UI/mods, network/link hub, wireless,
gyro, cosim, overlay JIT compilation (`std::system`/`dlopen` path), save storage.

Note: the harness does not construct a `gba::GbaBus`. Constructing one pulls in the full GBA
subsystem (90 additional symbols: IO, audio, save, RTC, gyro, solar sensor, PPU). That is the
first real size decision for a runnable hybrid, and it is not yet measured.

## 8. Original Gate D2 conditions

These were the proposed conditions before the host runs recorded in
[`SH4_GATE_D2_TRANSITIONS.md`](SH4_GATE_D2_TRANSITIONS.md). That later report is the
current execution status.

D2 may only start after a bus is bound and the harness actually calls the entry points.
Required fixtures, validated against the reference interpreter on host first:

1. native → native (`BL`/`BX LR` across two generated functions)
2. native → interpreted (dispatch miss into `runtime_bridge_interpret`)
3. interpreted → native (heal-to-static re-entry)
4. return across the native/interpreted boundary (call-stack frame ownership)
5. ARM/Thumb exchange (`exchange_instruction_set`, `CPSR_T`)

Compare all 16 guest registers, PC, CPSR, cycles, and observable memory, not just PC.
Hardware launch must use
`/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh` with a unique
`MGBA_DC_LOCK_TASK` label.

## 9. Handoff

- The original Gate D1 link and hash were reproduced, but a clean-source compile is not
  established: `gba_ppu.cpp` still fails on 64-bit layout and `std::min` typing, and the
  scratch tree contains an `alignas(8)` edit. Other reproduced blockers include libstdc++ C99
  namespace configuration, SH symbol-prefix mismatch, and include/link-line gaps.
- The smallest upstream-able change set is: a C99 namespace shim (or fixing
  `_GLIBCXX11_USE_C99_STDIO`/`_GLIBCXX11_USE_C99_STDLIB` for the sh-elf build), `-fexceptions`
  or an explicit non-exception error path for the two throw sites, `alignas(8)` in
  `foreign_obj_focus.h`, explicit casts at `gba_ppu.cpp:514-516`, and `src/debug` as a public
  include dir for `gbarecomp_gba`.
- The D1 link does not establish hybrid correctness. Current host Gate D2 passes all five
  corrected fixtures; see the audit report. Dreamcast hardware remains gated.
- The full GBA bus cost for a runnable hybrid is unmeasured; measure it before revisiting the
  16 MiB budget.
