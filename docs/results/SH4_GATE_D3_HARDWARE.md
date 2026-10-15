# Gate D3 — Production Hybrid Runtime on Dreamcast

**Verdict:** D3 linkability **GO**. All five physical fixtures passed twice on the same trace-enabled ELF, with exact per-instruction fingerprint equality against the independent host interpreter-only reference. D3 hardware correctness is **GO for this bounded fixture gate**. The measured footprint is for this fixture harness only; this is not a full-game memory or support claim.

## Required verdicts

| Gate | Result | Evidence |
|---|---|---|
| D1 existing-object ELF reproduction | **PASS** | Relinked the original D1 object set to the same SHA-256, `26e99822ab7af551975bb41110786f245f6834ec531d4ab28815b2f1b50f9db5`. Standard `sh-elf-size`: text 1,022,124 B, data 5,804 B, BSS 351,116 B; zero undefined symbols. |
| D1 fresh-source compile, unchanged source | **FAIL / blocker reproduced** | With the C99 namespace shim and exceptions enabled, original `gba_ppu.cpp` still fails the 72-byte descriptor assertion and three mixed-type `std::min` calls. See `/tmp/sma2-d3/unpatched/compile.log`. |
| Fresh-source compile/link with scratch portability fixes | **PASS for this D3 source set** | All 29 GBA translation units, required ARM/runtime units, generated fixture functions and BIOS objects compiled from the clean scratch archive; final bus-bound KOS ELF linked with no undefined symbols. This does not retroactively make the old D1 source-clean build pass. |
| D3 linkability | **GO** | Final ELF contains the actual interpreter, dispatch/miss bridge, generated functions, `GbaBus`, `GbaPpu`, real BIOS object and production state import/export. No `--defsym` aliases were used. |
| D3 physical correctness | **GO (bounded fixture gate)** | The original ELF passed all architectural checks twice. A bounded-ring trace ELF then passed all five fixtures twice, with all 54 per-instruction records per run matching the host reference exactly and identical hardware captures. |

## Source and toolchain provenance

- SMA2 branch: `dreamcast`; gitlink remains `647e2ab8c69666a55d04d82437002f1f1772c8bf` and uninitialized.
- GBARecomp source archive: `5e89943528aee7bc1c399bdbd035364a4b41dd10`. Its nested `arm-recomp-core` dependency was copied from the populated checkout at its pinned `1f13cdfd3b437a7a3919285f381987bd43f5234a`; neither checkout was changed or repinned.
- KOS: 2.3.0, console revision `cf190e23`, `KOS_ARCH=dreamcast`, `KOS_SUBARCH=pristine`. Toolchain: SH-4 GCC 17.0.0 (20260703), GNU binutils 2.47.20260726.
- Final ROM: `/tmp/sma2-d2/fixture/final.gba`, 848 bytes, SHA-256 `527ccc9f21cefd39e0ce1dfb86ef5ecfe52df3fb65ba0d0c3eee6eb25d806c2b`.
- BIOS: `/home/gpf/code/dreamcast/mGBADC/roms/gba_bios.bin`, SHA-256 `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`, SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492`. The Dreamcast harness loaded it through the dcload `/pc` mapping, verified the canonical SHA-1 through `GbaBios::load_from_file`, and bound it to the production bus.
- Generated cart table: nine entries; source SHA-256 `af9d82994ce01715d8879149252ab1de1406e1a51b3d430ce73f77f2ce50ff6b`. Functions: ARM `0x080000C0`, `0x08000100`, `0x08000180`, `0x08000240`, `0x08000270`, `0x080002C0`, `0x08000300`, `0x08000340`; Thumb `0x080001C0`.

The reference build used the independently linked `ref_table.o` (`kDispatchTableLen == 0`; object SHA-256 `c3b60f971104e2f323ca3aa34e6ddc49a50661955fe2633c3e774ceb03012009`). The hybrid table has nine rows. The final raw-BIOS-bound host binaries were `/tmp/sma2-d3/hybrid_raw_bios` (SHA-256 `eee711184e62b5836daab45de2cd21fbc661ea4f7465427ddb39723c3c30896e`) and `/tmp/sma2-d3/reference_raw_bios` (SHA-256 `644faf2137ed48cc9eb2642a6df3e6df03bb0d4cd99ca63770e85e7919d4619b`). Both attached the same BIOS to `GbaBus`; the reference table was empty, and the harness observed zero static-entry hits. These five fixtures do not enter BIOS address space.

## Scratch portability fixes

Only the fresh `/tmp/sma2-d3/src` archive was patched. Pristine files are preserved at `/tmp/sma2-d3/original/`; exact diffs are `patch-layout.diff` and `patch-min.diff`.

1. `foreign_obj_focus.h`: applied `alignas(8)` to the two `uint64_t` HUD masks. The source contract asserts a 72-byte descriptor (`gbarecomp/src/gba/foreign_obj_focus.h:113-115`). On SH-4, natural `uint64_t` alignment is 4 and the original struct measured 68 bytes. The scratch correction restores size 72. Host and SH-4 compile-time probes both verify size 72 and unchanged mask offsets 32 and 40. Host layout was already 72 bytes. No guest data or field offsets changed.
2. `gba_ppu.cpp`: made the three blend clamps explicit as `std::min<uint32_t>` at the original blend expressions (`gbarecomp/src/gba/gba_ppu.cpp:514-516`). `uint32_t` is `unsigned long` on this SH-4 toolchain while the literals are `unsigned int`; the implicit mixed-type deduction failed. The values are 32-bit and the explicit template converts the literals losslessly, preserving the same minimum operation.

Host and SH-4 compilation of the patched PPU passed. Recompiling the preserved original PPU reproduced the 68-versus-72 assertion and all three `std::min(unsigned int, unsigned long)` errors. No production GBARecomp, KOS, or mGBADC file was edited.

The compile shim at `/tmp/sma2-d3/glibcxx_c99_shim.h` re-exports the existing global C99 functions into `std`; it adds no implementation. `-fexceptions` was enabled. Linking through `kos-c++` required no symbol aliases: the final ELF has zero undefined symbols, so no `--defsym` can conceal missing exception or mutex functionality.

## Build and final ELF

The source archive plus pinned nested dependency and generated sources are under `/tmp/sma2-d3/`. KOS compilation used:

```sh
source /opt/toolchains/dc/kos/environ.sh
sh-elf-g++ -c $KOS_CFLAGS -std=c++20 -Wall -fexceptions \
  -include /tmp/sma2-d3/glibcxx_c99_shim.h \
  -I/tmp/sma2-d3 -I/tmp/sma2-d3/src/armv4t -I/tmp/sma2-d3/src/runtime \
  -I/tmp/sma2-d3/src/gba -I/tmp/sma2-d3/src/debug -I/tmp/sma2-d3/src/recompile \
  -I/tmp/sma2-d3/src/heal -I/tmp/sma2-d3/external/arm-recomp-core/core \
  -I/tmp/sma2-d3/external/arm-recomp-core/profiles/armv4t_gba \
  <source.cpp> -o <object.o>
kos-c++ -Wl,-Map=/tmp/sma2-d3/d3.map -o /tmp/sma2-d3/d3.elf <D3 objects>
```

The object set includes all 29 GBA translation units, the production ARM/Thumb decoder/interpreter, CPU bridge, runtime dispatch and self-heal bridge, actual bus/PPU/wait-state code, the nine generated fixture functions, cart dispatch table, and generated BIOS dispatch/body. The bus, PPU, and BIOS objects are static globals to keep their large memory arrays off KOS's main stack.

Final measured ELF: `/tmp/sma2-d3/d3.elf`, SHA-256 `4bf2af9ffe7cc06b0b488c86c29683a722341a1e6c8118869aa050aa45e77b9d`; map SHA-256 `31a80e2425443ab13de4431dc7792e5fff428c97ef18f5e2b3af8497f338a5f5`. ELF32 little-endian SuperH `EXEC`, entry `0x8c010000`, SH-4A flags, zero undefined symbols, no TLS.

## Runtime reachability

Link map and disassembly, not link success alone, establish these paths:

- `runtime_dispatch` at `0x8c0173ec` reaches `dispatch_resolve` at `0x8c01526c` and the nine-entry generated table.
- On a miss, disassembly at `dispatch_resolve+0x2f4` loads the real `runtime_bridge_interpret` address (`0x8c015688`) and calls it. The final disassembly sequence is at `0x8c015560-0x8c015566`.
- The linked bridge reaches `armv4t::Interpreter::step` (`0x8c0114cc`) and production CPU conversion functions `load_arm_cpu_into_interp` (`0x8c01081c`) / `store_interp_into_arm_cpu` (`0x8c014708`). `runtime_dispatch_miss` is LTO-inlined into the dispatch resolver, not replaced with a stub.
- Real `GbaBus::read32` is linked at `0x8c0a24cc`; `GbaBus::read8/read16/write8/write16/write32` are also present. The linked bridge loads the active-bus pointer. `tick_devices(gba::GbaBus*, gba::GbaPpu*, ...)` is linked at `0x8c01c220`; production bus/PPU event ticking is reachable.
- ELF BSS symbols show the actual bus (`g_bus`, 407,408 B), PPU object (100 B), BIOS object (16,412 B), and fixture ROM (848 B in `.rodata`).

## Host baseline and physical results

The corrected host matrix was rerun with the raw BIOS loaded and attached to both host buses. All five hybrid/reference fingerprint CSV pairs are byte-identical; their final states also match. The D2 fixture cases use only cart ROM addresses and produce no guest writes, so EWRAM, IWRAM, palette RAM, VRAM, and OAM remain zero in both configurations.

| Fixture | Host trace instructions | Cycles | Hardware run 1 | Hardware run 2 | Production miss / interpreter steps |
|---|---:|---:|---|---|---|
| D2-A native → native | 8 | 90 | PASS | PASS | 0 / 0 |
| D2-B native → interpreted | 6 | 64 | PASS | PASS | 1 / 2 |
| D2-C interpreted → native | 5 | 69 | PASS | PASS | 1 / 3 |
| D2-D native → interpreted → native | 9 | 107 | PASS | PASS | 1 / 3 |
| D2-E ARM ↔ Thumb | 26 | 149 | PASS | PASS | 1 / 10 |

Each hardware JSON record compares all 16 registers, full CPSR, PC, LR, expected cycles, terminal sentinel, zero call-return depth, writable-memory zero state, expected outer dispatch hit/miss scope, production distinct misses/interpreter steps, and exact native-entry sequence. A local comparison script independently parsed each hardware log and raw-BIOS-bound host reference output; all 10 fixture/run combinations match on all 16 registers, CPSR, PC, LR, and cycles.

D2-E's physical trace establishes state and target handling directly: ARM lookup at `0x08000240`, `CPSR.T=0`; production self-heal JSON records miss target `0x08000200`, `mode="thumb"`; native entry `0x080001C0` observes `CPSR.T=1`; then native entry `0x08000270` observes `CPSR.T=0`. D2-C logs the interpreted miss at `0x08000280` followed by native entry `0x08000180`. D2-D logs miss `0x080001A0` followed by native entry `0x08000180`.

The original 5/5 measured ELF runs intentionally left the default production fingerprint ring disarmed. That ring requests `2^23` entries at 80 bytes each (`runtime_arm.cpp:386-398`), or 640 MiB, so those runs alone could not establish trace equivalence. The bounded follow-up below captures and compares the complete fixture traces.

The first diagnostic KOS run exposed an overly strict expected native-entry list: the generated terminal sentinel at `0x08000300` legitimately fires the production entry hook on native returns. Its observed architectural state already matched the host. The scratch harness expectation was corrected and the final measured ELF above was then run twice; both complete logs contain `{"summary":"PASS","fixtures":5}` and `Program returned 0`. Earlier diagnostic ELFs/logs remain preserved under `/tmp/sma2-d3/` and are not counted as final runs.

Hardware target `192.168.0.128` was reached by `dc-load-ip 3.0.1`. Both final runs used the shared wrapper, not `kos-tool` directly:

```sh
source /opt/toolchains/dc/kos/environ.sh
MGBA_DC_LOCK_TASK=sma2-gate-d3-20261015-memory-run1 \
  /home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh \
  -t 192.168.0.128 -x /tmp/sma2-d3/d3.elf -m /home/gpf/code/dreamcast/mGBADC
MGBA_DC_LOCK_TASK=sma2-gate-d3-20261015-memory-run2 \
  /home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh \
  -t 192.168.0.128 -x /tmp/sma2-d3/d3.elf -m /home/gpf/code/dreamcast/mGBADC
```

Console captures: `/tmp/sma2-d3/hardware_measured_run1.log` (SHA-256 `6526c06703f09129689e08da13571b5a926dd0df04ef9a1290bbada49454e8cc`) and `hardware_measured_run2.log` (SHA-256 `4f64e944380b3d1d7d284655785f589caee3a9667ce6c0d09e2416772cda51c1`). The lock was free after both wrapper sessions.

## Memory accounting

Final ELF sections and PT_LOADs:

| Item | Bytes |
|---|---:|
| `.text` | 912,312 |
| `.rodata` | 47,356 |
| `.data` | 5,784 |
| `.bss` | 1,071,236 |
| `.tdata` / `.tbss` | none |
| PT_LOAD 1 `p_memsz` | 912,312 |
| PT_LOAD 2 `p_memsz` | 158,906 |
| PT_LOAD 3 `p_memsz` | 1,077,096 |
| **PT_LOAD `p_memsz` sum** | **2,148,314** |

The second segment includes `.init`, `.fini`, `.rodata`, `.eh_frame`, and exception tables; the third includes constructors, `.data`, GOT, and BSS. The 848-byte ROM is already in `.rodata`; the 16 KiB BIOS image is already held in the BSS `GbaBios` object. Neither is added a second time. No Dreamcast VRAM is counted as main RAM.

Physical KOS allocator telemetry was stable across both runs. After each fixture: `mallinfo.arena=8,614,748 B`; maximum observed `uordblks=8,613,700 B`; `fordblks` ranged 1,048–1,080 B. `mm_sbrk(0)` was `0x8CA55000`; KOS's heap-growth ceiling, with its 64 KiB main/kernel stack reserve, was `0x8CFF0000`. Thus measured additional heap-growth headroom after the harness was **5,877,760 B (5.60 MiB)**, plus about 1 KiB already free inside the allocator arena. These allocator figures include KOS startup/runtime and the harness; they are not attributed to SMA2 alone. The 64 KiB main-stack reservation is known; actual stack high-water was not measured. Peak non-fingerprint dynamic allocation beyond the KOS allocator telemetry was not separately attributed.

The fixture artifact is comfortably runnable on the 16 MiB console, with the measured heap-growth headroom above. This is only a minimal hybrid fixture footprint, not a full SMA2 fit estimate. The separate all-64-shard generated corpus remains a full-game NO-GO for retail RAM.

## Handoff

Keep the native selection fixed and gameplay integration gated. The bounded D3 fixture gate is complete, including full trace comparisons. This does not establish full-game compatibility or memory feasibility.

## Bounded fingerprint follow-up (complete)

An isolated follow-up build adds `GBARECOMP_RUNTIME_FP_CAPACITY`, defaulting to the
existing production capacity and set to 32 only for this scratch ELF. At 80 bytes per
record this bounds the ring to 2,560 B; a compile-time assertion checks the entry size.
The scratch harness arms the production per-instruction hook, requires the expected
8/6/5/9/26 records for D2-A through D2-E, and emits each complete record as a `D3FP`
console line. A small copy API exports the ring oldest-first without file I/O. The
experiment source changes are confined to `/tmp/sma2-d3`.

Prepared ELF `/tmp/sma2-d3/d3_fp.elf`, SHA-256
`60d71d42db71b10d3e912b487752b15879d41682a0bf5ac9c69f705844b417f7`; map
`/tmp/sma2-d3/d3_fp.map`, SHA-256
`e82d2d661a09025831544e0cc9d6a68dbc77e30f42ca25628996347c0268be48`. It links as a
SuperH executable with no undefined symbols. Host reference CSV lengths match the
expected ring counts. The final trace-enabled ELF was uploaded twice through the shared
wrapper after lock preflight succeeded. Both logs report `{"summary":"PASS","fixtures":5}`
and `Program returned 0`. Each run emitted 54 full records (8/6/5/9/26); the comparison
script `/tmp/sma2-d3/compare_fp.py` matched every record against the independent host
reference CSVs. Run-1 and run-2 record streams are byte-identical (SHA-256
`31d3a6d01bbe26f296b37d350124fe687c9949fd4028cf54ec80309308a530cc`). Final registers,
full CPSR/T, PC, LR, cycles, terminal state, writable-memory state, and call depth match
the host reference in both runs.

Console captures: `/tmp/sma2-d3/hardware_fp_run1b.log` (SHA-256
`d8677c15ed85f5167834f9438bda78689e18598d5480353ec5b715ffea04a60a`) and
`/tmp/sma2-d3/hardware_fp_run2.log` (SHA-256
`879d81cccc8819e4cf730789981a1828e442415eec0a9552601efc9e589fdc49`). Unique lock
tasks were `sma2-gate-d3-fp-20261015-run1b` and `sma2-gate-d3-fp-20261015-run2`.

The trace-enabled ELF has `.text` 912,504 B, `.rodata` 47,588 B, `.data` 5,784 B, and
`.bss` 1,071,248 B. PT_LOAD `p_memsz` sum is 2,143,526 B. Allocator telemetry measured
8,619,728 B arena, maximum 8,616,320 B used, 3,408 B free, and 5,877,760 B (5.60 MiB)
further heap-growth headroom. The fingerprint ring allocates 2,560 B; its 32-record
copy buffer is a 2,560-byte harness stack local. Stack high-water remains unmeasured.
Full console captures and the comparison script are preserved under `/tmp/sma2-d3/`.
