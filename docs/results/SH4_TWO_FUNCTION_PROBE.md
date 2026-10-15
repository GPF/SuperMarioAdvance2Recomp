# SH-4 Two-Function Probe — `BL` / `BX LR` / dispatch-table linkage

**Historical static report: STATIC GO / RUNTIME UNVERIFIED at the time it was written.**

This report records generation, compilation, static linking, disassembly, and relocation
accounting before physical execution. The gate was subsequently closed for the isolated
fixture: see [SH4_TWO_FUNCTION_HARDWARE.md](SH4_TWO_FUNCTION_HARDWARE.md) for the 2026-10-15
Dreamcast runs and observed guest state. The later result does not claim production runtime
or full-game support.

Workspace: `/tmp/gba-sh4-aot-probe/probe2/` (first-probe artifacts under
`/tmp/gba-sh4-aot-probe/` are preserved and untouched).

---

## 1. Fixture

`/tmp/gba-sh4-aot-probe/probe2/fixture2.gba`, 512 bytes, all zero except the two
function bodies.

| Guest addr | Bytes (LE) | Decoded | Instruction |
|---|---|---|---|
| `0x080000C0` | `11 10 a0 e3` | `0xE3A01011` | `MOV r1,#0x11` |
| `0x080000C4` | `0e c0 a0 e1` | `0xE1A0C00E` | `MOV r12,r14` (save caller LR) |
| `0x080000C8` | `0c 00 00 eb` | `0xEB00000C` | `BL 0x08000100` |
| `0x080000CC` | `22 20 a0 e3` | `0xE3A02022` | `MOV r2,#0x22` (after return) |
| `0x080000D0` | `0c e0 a0 e1` | `0xE1A0E00C` | `MOV r14,r12` (restore LR) |
| `0x080000D4` | `1e ff 2f e1` | `0xE12FFE1E` | `BX r14` (top-level return) |
| `0x08000100` | `33 30 a0 e3` | `0xE3A03033` | `MOV r3,#0x33` |
| `0x08000104` | `1e ff 2f e1` | `0xE12FFE1E` | `BX r14` (return to caller) |

`BL` encoding check: `(0xEB00000C & 0xFFFFFF) * 4 = 48`; `PC` at the `BL` is
`0x080000C8 + 8 = 0x080000D0`; `0x080000D0 + 48 = 0x08000100` → Function B. ✔

Symbol seeds (`symbols2.tsv`):

```
0x080000C0	arm	probe_func_a
0x08000100	arm	probe_func_b
```

### Expected guest state (hand-derived, written before generation)

Initial: `R[0..15] = 0`, `CPSR = 0` (ARM state), `R[14] = 0x08000140` (caller LR /
harness sentinel).

Expected control flow:

1. `dispatch(0x080000C0)` → generated table hit → Function A.
2. `R[1] = 0x11`, `R[12] = R[14] = 0x08000140`.
3. `BL` pushes return frame `0x080000CC`, calls Function B.
4. Function B sets `R[3] = 0x33`, returns via `BX LR` (LR = `0x080000CC`).
5. Function A resumes at `0x080000CC`, sets `R[2] = 0x22`, restores `R[14] = 0x08000140`.
6. `BX r14` at `0x080000D4` → top-level return to the sentinel `0x08000140`.

Expected final state:

| Item | Expected |
|---|---|
| `R[1]` | `0x00000011` |
| `R[2]` | `0x00000022` |
| `R[3]` | `0x00000033` |
| `R[12]` | `0x08000140` |
| `R[14]` | `0x08000140` |
| `R[15]` | `0x08000140` |
| `CPSR.T` | `0` (ARM state preserved across the call) |

These expectations were derived from ARMv4T semantics independently of the generated
SH-4 output. They have **not** been compared against an actual run.

---

## 2. Generation

Command (reproduced, byte-identical output):

```
/tmp/gba-sh4-aot-probe/host-build/gba_recompile \
  --rom fixture2.gba --entry 0x080000C0 --symbols symbols2.tsv --out gen2
```

Output: `gen2/{recompiled.h, recompiled_000.cpp, recompiled_001.cpp,
dispatch_table.cpp, symbol_map.cpp}`.

Generator report: `discovered 2 functions (arm=2 thumb=0 indirect=2 undefined=0
branch_targets=1)` — the `BL` target is recognised as a known function, not left as
an indirect dispatch.

Generated call path (`gen2/recompiled_000.cpp`):

```cpp
52:    runtime_call_push_return(0x080000CCu);
55:    gf_probe_func_b();
56:    if (g_cpu.R[15] != 0x080000CCu) { runtime_call_cancel_return(0x080000CCu); return; }
94:    if (runtime_call_should_return(g_cpu.R[15])) return;
```

Generated return path (`gen2/recompiled_001.cpp`):

```cpp
26:    g_cpu.R[3] = _r_08000100;
39:    if (runtime_call_should_return(g_cpu.R[15])) return;
```

GBARecomp checkout used: `/home/gpf/code/gbarecomp` at
`5e89943528aee7bc1c399bdbd035364a4b41dd10`. This is **not** SMA2's pinned submodule
revision `647e2ab8c69666a55d04d82437002f1f1772c8bf`; no equivalence is assumed.
No source in GBARecomp was modified.

---

## 3. Compilation

Per translation unit:

```
sh-elf-g++ -c -O2 -Wall -fno-exceptions -fno-rtti \
  -ffunction-sections -fdata-sections \
  -I gen2 -I . -I /home/gpf/code/gbarecomp/src/armv4t <file> -o obj2/<name>.o
```

All five translation units compile, exit 0. Warnings only:

- `harness2.cpp`: three `-Wformat=` mismatches (`%08X` vs `uint32_t` = `long unsigned int`
  on SH). Cosmetic, no effect on generated code.
- `gen2/recompiled_000.cpp`: `_co_080000C4` and `_co_080000D0` set-but-not-used. These are
  the codegen's per-instruction output temporaries for `MOV r12,r14` and `MOV r14,r12`;
  the values are still written to `g_cpu` before the temporary is dropped. Not a defect,
  but worth noting for later coverage work.

Tool versions (recaptured):

```
cmake 3.28.3 · host g++ 13.3.0 · GNU Make 4.3
sh-elf-g++ (GCC) 17.0.0 20260703 (experimental)
GNU ld / objdump / readelf (GNU Binutils) 2.47.20260726
KOS_BASE=/opt/toolchains/dc/kos  KOS_ARCH=dreamcast  KOS_SUBARCH=pristine
KOS_CC_BASE=/opt/toolchains/dc/sh-elf
KOS 2.3.0
```

---

## 4. Static link

Final ELF: `/tmp/gba-sh4-aot-probe/probe2/probe2.elf`, 2,010,208 bytes, link exit 0.

```
ELF32, 2's complement little-endian
Machine: Renesas / SuperH SH
Type: EXEC
Flags: 0xc (sh4a)
Entry: 0x8c010000  (_start, KOS arch startup)
Program headers: 3
Dynamic section: none
Undefined symbols in final ELF: 0
```

Loadable segments:

| VirtAddr | FileSiz | MemSiz | Flg | Sections |
|---|---|---|---|---|
| `0x8c010000` | 180,876 | 180,876 | R E | `.text` |
| `0x8c03e000` | 21,068 | 21,068 | R E | `.init .fini .rodata .eh_frame` |
| `0x8c0432cc` | 5,596 | 24,044 | RW | `.ctors .dtors .data .got .bss` |

Note: the exact link command used for `probe2.elf` was not preserved in the artifact
log. A reproduction attempt from scratch fails on `_tdata_align` / `_tbss_align`, which
are supplied by the KOS arch linker script rather than by `libkallisti`/`libc`. That is a
KOS build-system detail, not a generated-code problem. `probe2.elf` itself is the
original, successfully linked artifact and is intact.

---

## 5. Symbols, relocations, sizes

### Generated function sizes (per-object code sections)

| Object | Section | Size |
|---|---|---|
| `obj2/recompiled_000.o` | `.text.gf_start_vector` | **580 bytes** (6 guest instructions) |
| `obj2/recompiled_001.o` | `.text.gf_probe_func_b` | **296 bytes** (2 guest instructions) |
| `obj2/dispatch_table.o` | `.rodata.kDispatchTable` | 24 bytes (2 × 12) |
| `obj2/dispatch_table.o` | `.rodata.kDispatchTableLen` | 4 bytes |
| `obj2/harness2.o` | harness code (all `.text.*`) | 1,060 bytes |
| `obj2/symbol_map.o` | `.text.startup` | 16 bytes |

**Generated game code total: 876 bytes.**

First-probe baseline was 296 bytes for a 2-instruction function. Function B here is the
same shape (`MOV rN,#imm` + `BX LR`) and measures exactly **296 bytes** — the per-function
cost is dominated by the fixed prologue/epilogue and the runtime-hook preamble, not by the
instruction body. Function A's 580 bytes for 6 instructions (~97 B/instr) vs Function B's
296 for 2 (~148 B/instr) confirms that fixed overhead, not instruction count, drives size.
Do not extrapolate full-game size from these two numbers.

### Code-section relocations (all `R_SH_DIR32`)

- `recompiled_000.o` → 15 entries: `g_runtime_fn_entry_hook`, `runtime_should_yield`,
  `g_cpu`, `g_runtime_insn_trace`, `g_runtime_waits`, `runtime_tick`,
  `runtime_call_push_return`, **`gf_probe_func_b`** (cross-function call), `g_cpu`,
  `g_runtime_waits`, `runtime_call_should_return`, `runtime_dispatch_with_exchange`,
  `runtime_insn_fp`, `runtime_call_cancel_return`.
- `recompiled_001.o` → 11 entries: same runtime set minus the call-push and cancel paths.
- `dispatch_table.o` → 2 entries: `_gf_start_vector`, `_gf_probe_func_b`.

Total code/data relocations: **28, all `R_SH_DIR32`**. `R_SH_REL32` appears only in
`.rela.eh_frame` (non-alloc unwind/debug), which is excluded from the loadable image.
Every external symbol referenced by generated code is accounted for; the final ELF has
zero undefined symbols.

### Final ELF section sizes

| Section | Size |
|---|---|
| `.text` | 180,876 |
| `.init` | 54 |
| `.fini` | 42 |
| `.rodata` | 19,992 |
| `.eh_frame` | 976 |
| `.data` | 5,564 |
| `.got` | 12 |
| `.bss` | 18,424 |

Splitting generated code from the link floor:
`180,876 − 876 (generated) − 1,060 (harness) − 16 (symbol_map) = 178,924 bytes` of KOS
static-link floor. The floor, not the probe, dominates the ELF.

---

## 6. Disassembly (`sh-elf-objdump -d`, `disasm_elf2.txt`)

### Dispatch-table linkage — verified statically

`.rodata` at `0x8c0402ec` (`_kDispatchTable`, `_kDispatchTableLen = 2` at `0x8c0402e8`):

```
entry 0: addr 0x080000C0, thumb 0, resume 0, fn 0x8c010534 = _gf_start_vector
entry 1: addr 0x08000100, thumb 0, resume 0, fn 0x8c010778 = _gf_probe_func_b
```

Both entries point at the two generated functions. Linkage is real, not assumed.

### Guest `BL` → host SH-4 subroutine call

```
8c0105e0:  mov.l 8c010754,r1        ! 8c010778 <_gf_probe_func_b>
8c0105e2:  jsr  @r1
8c0105e4:  nop                      ; delay slot
8c0105e6:  mov.l @(60,r8),r1        ; g_cpu.R[15]
8c0105e8:  mov.l 8c010744,r2        ! 80000cc
8c0105ea:  cmp/eq r2,r1
8c0105ec:  bf.s 8c0106b8            ; -> runtime_call_cancel_return on mismatch
```

The `BL` became a genuine `jsr` (return through `pr`), not a tail `jmp`. Post-call the code
reconciles `g_cpu.R[15]` against the pushed return address `0x080000CC` and branches to
`runtime_call_cancel_return` if the guest PC does not match — the documented contract.

### Guest register writes through the `g_cpu` ABI target

`r8` holds `&g_cpu` throughout, so `@(n*4,r8)` is `g_cpu.R[n]`. The disassembly contains
writes at offsets 56 and 60, i.e. `R[14]` and `R[15]`:

```
8c0105d2:  mov.l r2,@(56,r8)        ; g_cpu.R[14]
8c0105d6:  mov.l r3,@(60,r8)        ; g_cpu.R[15]
```

Which source instruction each of those two stores belongs to is an inference from the
offset, not something the disassembly labels; the generated C confirms the corresponding
stores (`g_cpu.R[12]`, `g_cpu.R[14]`, `g_cpu.R[15]`) exist for the `MOV r12,r14` /
`MOV r14,r12` / PC-advance blocks. The unambiguous case is in Function B:

```
8c0107a6:  mov #51,r1               ; 0x33
8c0107ac:  mov.l r1,@(12,r8)        ; g_cpu.R[3]
```

Guest `R[3] = 0x33` is written to the documented ABI target.

### `BX LR` return path

```
8c01080e:  mov.l ...,r0             ! _runtime_call_should_return
8c010810:  jsr  @r0
8c010812:  mov.l @(60,r8),r4
8c010814:  tst  r0,r0
8c010816:  bf.s 8c010826            ; no frame -> dispatch path
8c010818:  mov  r9,r4
8c01081a:  mov.l ...,r1             ! _runtime_dispatch_with_exchange
8c01081c:  lds.l @r15+,pr
8c01081e..8c010822:  restore r10,r9 ; jmp @r1 (delay slot restores r8)
8c010826:  lds.l @r15+,pr
8c010828..8c01082c:  restore r10,r9 ; rts   ; delay slot restores r8
```

Two distinct exits exist, matching the generated C: host `rts` back to the caller when the
call-return stack reports a frame, otherwise `jmp` to `runtime_dispatch_with_exchange`.
Delay slots are populated correctly (`nop` after `jsr`, register restore after `rts`/`jmp`).

### ABI boundary

Generated functions use the SH C ABI: callee-saved `r8–r13` plus `pr` are pushed on entry
and popped on exit; `r8` holds `&g_cpu` throughout. The guest `BX LR` return is therefore
implemented as a **host C return**, contingent on the harness call-return stack — guest `LR`
alone is not the return mechanism for a direct `BL`. That is the contract as generated; it
has not been exercised.

---

## 7. Evidence classification

| Requirement | Status |
|---|---|
| Both guest functions generated | **VERIFIED** (2 functions, deterministic regeneration confirmed) |
| Both compiled with `sh-elf-g++` | **VERIFIED** (exit 0, warnings only) |
| Final KOS ELF linked | **VERIFIED** (exit 0, EXEC ELF32 LE SH, no dynamic section) |
| External symbols / relocations accounted for | **VERIFIED** (28 code relocations, all `R_SH_DIR32`; 0 undefined symbols) |
| Dispatch-table linkage | **VERIFIED statically** (both table entries resolve to the generated functions) |
| `BL` → host call, `BX LR` → return path | **VERIFIED by disassembly only** |
| Delay slots / callee-saved register handling | **VERIFIED by disassembly only** |
| Guest-state mutation (`R[1]`,`R[2]`,`R[3]`,`R[12]`,`R[14]`,`R[15]`,`CPSR.T`) | **UNVERIFIED — no execution** |
| Control-flow sequence actually taken | **UNVERIFIED — no execution** |
| Hardware behavior | **UNVERIFIED** |

**BEHAVIORAL GATE UNVERIFIED.** No guest-state behavior has been executed. The harness
contains hand-derived expectations, but nothing has run them.

---

## 8. Testing-environment limitation (not a generated-code failure)

`qemu-sh4` linux-user 8.2.2 cannot load the probe ELF:

```
$ /usr/bin/qemu-sh4 ./probe2/probe2.elf
qemu-sh4: .../probe2.elf: requires more than reserved virtual address space
          (0x8c0490b7 > 0x7fffffff)
exit=1
```

KOS links at `0x8c000000`, above the linux-user reserved address space. Minimal SH-4 test
ELFs also fail with `Error mapping file: Invalid argument`. Prior QEMU artifacts are
preserved under `/tmp/gba-sh4-aot-probe/qemu_env_check/`. This is a loader/environment
limitation; it says nothing about the correctness of the generated code.

---

## 9. Reference comparison (read-only, no code copied)

`/home/gpf/code/dreamcast/gpsp-sh4-jit` and `/home/gpf/code/dreamcast/brucemGBA` both use a
SH-4 JIT with a per-function entry and a dispatch mechanism. The difference relevant here:
those designs return through a JIT-managed return path rather than through the host C call
stack. The GBARecomp AOT path under test relies on the host `pr`/C return for a direct `BL`,
which is simpler but couples guest call semantics to the host ABI. No comparison expanded
this probe's scope, and neither repo was modified.

---

## 10. Artifacts

```
/tmp/gba-sh4-aot-probe/probe2/
  fixture2.gba          symbols2.tsv        harness2.cpp
  gen2/                 gen2_repro/         obj2/
  probe2.elf            disasm_elf2.txt
  inspect_elf2.txt      inspect_obj2.txt    inspect_obj2_code.txt
  inspect_data2.txt     compile2.log        repro_gen.log
/tmp/gba-sh4-aot-probe/qemu_env_check/      (preserved, untouched)
```

---

## 11. Next gated task

The remaining requirement is **execution evidence**. Options, in preference order:

1. Run the probe ELF on physical Dreamcast hardware (Troy's hardware beats emulators),
   with recorded configuration and results.
2. Otherwise, obtain an SH-4 execution environment that can load a KOS-address-space ELF
   (e.g. a KOS-aware emulator such as a Flycast/Redream-style loader, or a relocatable
   link at a linux-user-safe address) — this is a separate, explicitly authorised task,
   not a continuation of the QEMU work already abandoned here.

Until one of those produces a run, the two-function probe stays
**STATIC GO / RUNTIME UNVERIFIED**, and no full-game or hardware claim is permitted.
