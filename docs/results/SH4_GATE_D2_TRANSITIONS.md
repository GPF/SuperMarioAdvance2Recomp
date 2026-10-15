# Gate D2 — native/interpreter transition evidence

**Verdict: D2 host gate PASS** — all five corrected host fixtures match the
interpreter-only reference. Gate D1 link reproduction also succeeds with the
recorded ELF hash. Hardware remains gated by explicit instruction.

## 1. Gate D1 link reproduction

Scratch tree: `/tmp/sma2-d2` (new; the earlier `/tmp/sma2-d1` tree is preserved
untouched as evidence). Toolchain recorded: `sh-elf-g++ (GCC) 17.0.0
20260703 (experimental)`, `GNU ld 2.47.20260726`, KOS
`/opt/toolchains/dc/kos`, `KOS_ARCH=dreamcast`, `KOS_SUBARCH=pristine`.

The link was rerun with the object names actually present in `/tmp/sma2-d1/build`
(including `overlay_loader.cpp.o` and `mod_function_hooks.cpp.o`). The first
attempt linked a newer D2 object set under a different output path and produced a
different hash; rerunning the original D1 inputs, output path, and map path
reproduced the recorded ELF exactly:

| Item | Reproduced value |
|---|---|
| ELF | `/tmp/sma2-d1/build/d1_gate.elf` |
| SHA-256 | `26e99822ab7af551975bb41110786f245f6834ec531d4ab28815b2f1b50f9db5` |
| text / data / bss | 1,022,124 / 5,804 / 351,116 B |
| Undefined symbols | 0 |

Exact link invocation, run from `/tmp/sma2-d1/build`:

```text
kos-c++ -Wl,-Map=d1_gate.map -o d1_gate.elf main_min.o reduced_functions.o \
  dispatch_table.o arm_decode.o arm_ir.o interpreter.o thumb_decode.o runtime_arm.o \
  symbol_lookup.o arm_cpu_bridge.o runtime_arm_default_aborts.o runtime_bus_bridge.o \
  gba_waitstates.o overlay_loader.cpp.o mod_function_hooks.cpp.o \
  /tmp/sma2-d1/obj_bios/bios_dispatch_table.o /tmp/sma2-d1/obj_bios/bios_recompiled.o
```

This reproduces the link, not a clean-source compile. The baseline compile with
KOS flags + `-std=c++20 -Wall -fno-exceptions -fno-rtti` reproduces the reported
diagnostic shape: **48 error diagnostics across 5
translation units** (`arm_ir.cpp` 24, `gba_ppu.cpp` 4, `runtime_arm.cpp` 8,
`runtime_arm_default_aborts.cpp` 3, `runtime_bus_bridge.cpp` 9), in the reported
categories plus the newly exposed `gba_ppu.cpp` failures: `std::snprintf` 36,
`std::strtoull` 2, `std::atoll` 4, exception-required 2, and `gba_ppu.cpp`
layout/type errors 4 (48 total).

With the force-included C99-namespace shim and `-fexceptions`, every tested
translation unit compiles cleanly **except `gba_ppu.cpp`**, which still fails on
two non-namespace issues:

- `static_assert(sizeof(GbaForeignObjFocusTransform) == 72)` reduces to
  `(68 == 72)` on this toolchain.
- `no matching function for call to 'min(unsigned int, long unsigned int)'`.

Those two are the previously suggested `alignas(8)` and `std::min` changes. They
were **not applied to production GBARecomp source**. The existing scratch tree at
`/tmp/sma2-d1/patched/src/gba/foreign_obj_focus.h` contains an `alignas(8)`
patch; a file-by-file comparison against the checked-out GBARecomp shows that is
the **only** file differing from upstream, so the reported Gate D1 ELF was built
from a patched tree, not from the clean checkout.

The force-included shim and `-fexceptions` still leave `gba_ppu.cpp` failing on
the alignment and `std::min` issues above. The original scratch source tree also
contains an `alignas(8)` change. Therefore the reproduced ELF hash does not verify
the earlier claim that a clean GBARecomp source tree compiled without edits.

## 2. Host harness

Headless, no SDL2/PVR/audio/UI/gameplay loop. Real `gba::GbaBus` and `gba::GbaPpu`
bound through the production `gbarecomp::set_active_bus` / `set_active_ppu` and
`runtime_init`. No substitute bus, no constant-returning memory callbacks, no
fabricated coverage counters, no post-execution state correction.

Two configurations were built from the same sources:

- **hybrid** — production dispatch table (9 generated functions) + production
  `runtime_dispatch_miss` self-heal bridge.
- **reference** — independently configured interpreter-only path: production
  dispatch path with an **empty** dispatch table, so every guest PC resolves to
  the production bridge. Generated functions are unreachable in this build.

Both binaries were rebuilt against the supplied real BIOS
(`/home/gpf/code/dreamcast/mGBADC/roms/gba_bios.bin`, SHA-256
`fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`). Comparison
uses the production fingerprint ring (`GBARECOMP_INSN_TRACE=1`) plus final state,
cycles, and call-return depth. The harness's dispatch counters are specifically
`outer_dispatch_calls` and classify only those outer calls; misses inside generated
code/self-heal are confirmed by production self-heal logs.

Host binaries: hybrid SHA-256 `3bca1663627f91c5428a1ccd5b0b99cf71bb7f50f8b2070d38357504078606d5`;
interpreter-only reference SHA-256
`71ad3d09b4fe71cee0cea0d5a0c8b947fa33f5452a7786b3f2d57056ce3dbcc6`. Run logs and
fingerprint CSVs are under `/tmp/sma2-d2/rerun/` and `/tmp/sma2-d2/log/`.

## 3. Fixture

Corrected deterministic fixture assembled with `arm-none-eabi-as -mcpu=arm7tdmi`,
SHA-256 `527ccc9f21cefd39e0ce1dfb86ef5ecfe52df3fb65ba0d0c3eee6eb25d806c2b`,
848 bytes. Nine functions are in the dispatch table; four regions are deliberately
absent and are reached only through computed `BX` targets or literal-pool loads,
so they exercise the production dispatch-miss path.

Generated (dispatch hits): `0x080000C0`, `0x08000100`, `0x08000180`,
`0x080001C0` (thumb), `0x08000240`, `0x08000270`, `0x080002C0`, `0x08000300`,
`0x08000340`.

Interpreted (dispatch misses, absent from the table): `0x08000140`, `0x080001A0`,
`0x08000200` (thumb), `0x08000280`.

Terminal sentinel `0x08000300`; `g_runtime_break_pc` is set to it so the
production per-instruction prologue ends execution.

## 4. Results

| Fixture | Transition | Hybrid | Reference | Agreement |
|---|---|---|---|---|
| D2-A | native → native (positive control) | 8 instructions, 90 cycles | 8 instructions, 90 cycles | **PASS** |
| D2-B | native → interpreted (dispatch miss) | 6 instructions, 64 cycles | 6 instructions, 64 cycles | **PASS** |
| D2-C | interpreted → native (heal-to-static) | 5 instructions, 69 cycles | 5 instructions, 69 cycles | **PASS** |
| D2-D | native → interpreted → native | 9 instructions, 107 cycles | 9 instructions, 107 cycles | **PASS** |
| D2-E | ARM ↔ Thumb exchanges | 26 instructions, 149 cycles | 26 instructions, 149 cycles | **PASS** |

For all five fixtures, hybrid and reference final register state and complete
per-instruction fingerprint CSVs are byte-identical. The CSVs record all 16 guest
registers, PC, CPSR, and cumulative cycles. These fixtures make ROM reads but no
guest writes, so writable-memory side effects are not exercised.

D2-B's trace shows the boundary crossing explicitly: native instructions at
`0x08000340`–`0x0800034C`, then PC `0x08000140` (a region absent from the
dispatch table, reached through `BX r3`), then the interpreted instruction at
`0x08000144`, then return to the sentinel. The production self-heal log confirms
the miss:

```text
runtime_arm: SELF-HEAL dispatch miss for pc=0x08000140 (arm) - no generated function;
  bridging through the interpreter ...
runtime_arm: SELF-HEAL bridge entered at top level (empty call-return stack)
  for pc=0x08000140; LR=0x08000300 (fallback stop) - will heal to the next static entry.
```

## 5. Diagnosed fixture defects and corrected traces

**D2-C / D2-D:** both ARM `LDR` instructions used `[pc, #12]`, but the literal
immediately follows the `BX`; the correct offset is `#0`. `objdump` showed the
old D2-C load at `0x284` read `0x298`, beyond the fixture, instead of literal
`0x28C`. The loaded zero sent `BX r3` to BIOS address 0. In the real BIOS path,
the BL at `0x9C` sets LR to `0xA0` and pushes that return address while entering
the BIOS routine at `0xE0`; scratch-only logging observed the push with guest PC
`0xE0`, LR `0xA0`, CPSR `0x10`, and increasing runtime stack depth. That BIOS
call chain caused the old overflow. Setting the literal load offset to `#0`
restores the intended target `0x08000180` and both fixtures pass.

**D2-E:** the original ARM code formed `0x08000200` and the interpreted Thumb
code formed `0x080001C0`; both targets were even, so `BX` selected ARM state. The
old “1000 dispatches, zero misses” result counted only outer harness-loop entries
and missed self-heal dispatches inside production code. The fixture now sets bit 0
on both Thumb targets. Trace: ARM state `CPSR.T=0` at `0x08000254`, `r3=0x08000201`;
the production miss log reports `0x08000200 (thumb)`, and the first interpreted
Thumb fingerprint has `CPSR.T=1`. The Thumb region then forms target `0x080001C1`,
re-enters generated Thumb at `0x080001C0`, and later exchanges back to ARM at
`0x08000270`. The harness counters were renamed to make their outer-call scope
explicit.

## 6. Hardware

Not attempted. Host agreement now passes, but the user explicitly directed that
Dreamcast hardware remain gated. The shared launcher lock was not acquired and no
upload was performed.

## 7. Memory accounting

No bus-bound D2 KOS ELF was linked in this diagnostic pass, so there is no new
memory accounting against retail 16 MiB main RAM. VRAM was not counted as main RAM.

## 8. Handoff

1. The D1 link hash is reproduced; a clean-source compile is still open because
   `gba_ppu.cpp` fails on the measured layout and type issues.
2. All five corrected host fixtures agree with the interpreter-only reference.
3. Hardware remains gated as directed. No hardware run or upload was attempted.
