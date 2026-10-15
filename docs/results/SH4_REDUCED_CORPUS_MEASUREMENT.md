# SH-4 Reduced Corpus Measurement

## Verdict

**SIZE-ONLY.** A deterministic entry-rooted selection compiled to SH-4 objects with
a matching 13-row dispatch table. The generated code and dispatch table shrink sharply,
but there is no linked SMA2/KOS executable and no executed reduced-table interpreter
fallback. This result is not evidence that a hybrid SMA2 runtime fits or runs on Dreamcast.

## Provenance

- Repository: `/home/gpf/code/SuperMarioAdvance2Recomp`, branch `dreamcast`.
- Source generated directory: `/tmp/SMA2-PC-Trial/generated`.
- Source corpus identity (SHA-256 over sorted per-file SHA-256 lines for all 64
  `recompiled_*.cpp` files, `recompiled.h`, `dispatch_table.cpp`, and `symbol_map.cpp`):
  `2455c374c755f44b6cd11ca4df4d5a55d4c74055e174245297b49a9cd9404721`.
- Input ROM SHA-256:
  `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`.
- The historical generator revision is **not recorded** in the generated corpus; generated
  headers contain no revision field. The inspected GBARecomp checkout is
  `5e89943528aee7bc1c399bdbd035364a4b41dd10`, but that is not claimed as the generator
  revision. The SMA2 GBARecomp gitlink remains pinned at
  `647e2ab8c69666a55d04d82437002f1f1772c8bf`; the submodule remains uninitialized and
  unchanged.
- KOS environment: `/opt/toolchains/dc/kos`, `KOS_ARCH=dreamcast`,
  `KOS_SUBARCH=pristine`; `sh-elf-g++` GCC 17.0.0 20260703 (experimental), GNU binutils
  2.47.20260726. Target flags included `-m4-single -ml -mfsrra -mfsca`,
  `-ffunction-sections -fdata-sections`, `-flto=auto -ffat-lto-objects`, and the same
  `-fno-exceptions -fno-rtti -std=c++20` additions as the all-64 compile.
- The generator tree identity and compiler metadata are preserved in
  `/tmp/sma2-sh4-reduced-reset-root-20261015/source_sha256.txt`, `manifest.json`, and
  `toolchain_and_source_metadata.txt`. `derive_reduced.py` and `derive.log` preserve the
  subset/table derivation command and result.

## Selection and dispatch

The deterministic root is `gf_start_vector` at `0x08000000` ARM. Selection takes the
transitive closure of generated C function-symbol references from each selected function,
traversed breadth-first with lexically sorted neighbors. The result is:

| Address | Mode | Generated function | Direct generated callees |
|---|---|---|---|
| `0x08000000` | ARM | `gf_start_vector` | `0x080000C0` |
| `0x080000C0` | ARM | `gf_afunc_080000C0` | none |

The currently inspected GBARecomp CLI supports `--entry` and emits deterministic
translation-unit shards plus a matching dispatch table from discovered functions. It also
accepts `--max-functions`; discovery code treats functions reached by the required entry
walk as required, so a cap is not a safe substitute for auditing the resulting call graph.
The original corpus does not identify the generator revision/configuration, so this task
derived the selection from the immutable generated outputs and did not claim to reproduce
the historical generator run.

The root's `B` is emitted as `GBARECOMP_TAIL_CALL(gf_afunc_080000C0)`. The second
function's generated transfers use runtime dispatch, rather than direct C symbol calls.
The 23,040 omitted functions therefore introduce no unresolved direct generated-function
references in this selection. The original dispatch table has 302,344 rows; filtering
its existing rows by selected `fn` preserves all 13 rows belonging to the selected
functions, including 12 function/resume rows for `0x080000C0` and the root row. Their
ARM/Thumb and resume flags are copied unchanged. The original generated files were not
edited.

Host-side validation confirmed that both retained function bodies are exact slices of the
source corpus, all direct C function-symbol references resolve within the selected set,
all table function pointers name retained functions, `0x080000C0` has a non-resume ARM
entry, and the omitted `0x080000F0` address is absent. The complete derived inputs are in
`/tmp/sma2-sh4-reduced-reset-root-20261015/`.

## Fallback and boundary contract

Source inspection of GBARecomp at the revision above shows this intended contract:

1. A selected PC and current CPSR.T mode hit the reduced sorted dispatch table and invoke
   the generated function. Its resume rows preserve entry at supported interior PCs.
2. A table miss reaches `runtime_dispatch_miss()` after the overlay tier declines the PC.
   That function records the miss and calls `runtime_bridge_interpret()`.
3. The bridge copies the live generated `g_cpu` state into the ARMv4T interpreter state,
   sets the normalized PC and Thumb bit, interprets instructions against the active GBA
   bus, and synchronizes architectural state back. A bridged guest call returns at the
   generated call-return stack's saved guest return PC; an empty-stack/top-level bridge
   can stop when it reaches a static entry and hand the PC back to native dispatch.
4. Instruction-set exchange is determined by the dispatched target bit/CPSR.T. The
   bridge separately carries ARM/Thumb state in its interpreter CPU state. Guest LR and
   status registers live in the synchronized CPU state; the guest return contract does
   not depend on inventing a host C frame for omitted functions.
5. Existing runtime hooks test force-interpreter and mutable RAM execution before static
   lookup. Copied code requires its reviewed runtime metadata/predicates to route it away
   from static dispatch; this experiment does not establish SMA2's complete copied-code
   coverage or configure those hooks for a Dreamcast runtime.

This is a source-level contract only. The production bridge was not linked or executed
with the reduced table. On Dreamcast, the desktop overlay compiler must be absent or
decline misses so they reach the interpreter bridge. `overlay_try_dispatch()` is checked
before the miss handler. The target's overlay policy, active bus, runtime libraries,
call-return behavior, ARM/Thumb transitions, and state equivalence remain unvalidated.
Thus omitted functions have a documented intended fallback path, but this selection is
**not HYBRID-CORRECT** evidence.

## Build and measurements

The reduced function and table objects compiled successfully with KOS's established SH-4
flags. The exact expanded compile commands are in `compile_commands.txt`; object sizes,
undefined symbols, compile logs, and checksums are preserved beside them. `-ffunction-sections`
and `-fdata-sections` were active. LTO was enabled in compile flags and fat LTO objects
were emitted. No link ran, so linker LTO, garbage collection, final sections, and ELF load
segments were not measured.

| Item | Measured value | Scope |
|---|---:|---|
| Full generated functions | 23,042 | Source header and prior corpus count |
| Selected functions | 2 | Exact selection above |
| Full dispatch rows | 302,344 | Original generated table |
| Selected dispatch rows | 13 | Original rows filtered by selected function |
| Selected generated `.text` | 1,468 B | Object `.text.*` sections, both functions |
| Selected dispatch `.rodata` | 160 B | 13 × 12-byte rows plus 4-byte length |
| Interpreter `step()` translation unit `.text` | 8,784 B | Separate SH-4 object, includes unused sections |
| Interpreter object `.rodata` | 22 B | Separate object |
| ARM decoder object `.text` / `.rodata` | 3,632 B / 32 B | Separate object |
| Thumb decoder object `.text` / `.rodata` | 5,460 B / 45 B | Separate object |
| `arm_ir.cpp` | Not compiled | KOS compiler reports `std::snprintf` absent from `std` |
| KOS startup/link contribution | Not measured | No link |
| Other runtime / bridge / bus code | Not measured | No link |
| `.data`, `.bss`, TLS, GOT and PT_LOAD | Not measured | No final ELF |
| Heap, stack, buffers, VRAM | Not measured | No runtime |

Against the all-64 compile baseline, the selected generated text falls from 38,343,560 B
to 1,468 B (**99.9962% reduction**); generated dispatch data including its length falls
from 3,628,132 B to 160 B (**99.9956% reduction**). Together those two measured native
artifacts fall from 41,971,692 B to 1,628 B (**99.9961% reduction**). This tiny reset
root retains 2 / 23,042 functions (0.00868%) and is only a measurement case; the ratio is
not a selective-game size projection.

Adding only the known ROM (4,194,304 B), prior KOS text floor (178,924 B), and these
selected native objects gives 4,374,856 B. That is **not** a RAM fit estimate: the
interpreter bridge, bus, GBARecomp runtime, runtime data, heap, stack, and loader placement
are absent. There is no measured interpreter-inclusive executable or 16 MiB headroom.

## Build blockers and tests

- The selected generated-functions and dispatch-table objects compiled with exit 0.
- The ARMv4T interpreter, ARM decoder, and Thumb decoder translation units compiled as
  separate objects. These figures do not include the dispatch-miss bridge and are not
  additive to a final linked result because linker garbage collection/LTO was not run.
- `arm_ir.cpp` failed because this SH-4 C++ library/compiler combination does not provide
  `std::snprintf` as used by the source. No shared source workaround was made.
- Direct KOS-flagged compilation of `runtime_arm.cpp` failed on `std::strtoull` (2 sites),
  `std::snprintf` (5 sites), and exception handling required at the dispatch force-interp
  path under `-fno-exceptions`. `runtime_arm_default_aborts.cpp`, which owns the miss
  bridge, failed on `std::snprintf` (3 sites). Logs and full diagnostic lists are in
  `/tmp/sma2-sh4-reduced-reset-root-20261015/log/`.
- The generated object still imports 16 runtime symbols, including guest CPU state,
  bus reads/writes, dispatch, timing, flags, tracing and resume state. The interpreter
  object imports `mul_wait_cycles`, `instr_cycle_base`, `next_fetch_nonsequential`, and
  compiler helper `__popcountsi2`. These are not linked here.
- No KOS executable, runtime bridge, dispatch-hit/miss execution, interpreter transition,
  state-equivalence test, or Dreamcast run was performed. Existing two-function hardware
  success is independent and does not validate this corpus.

## Next bounded milestone

First make the production miss bridge and its minimum interpreter/decode/IR/bus dependency
set compile and linkable under KOS without changing fallback semantics. Capture a real
KOS-linked reduced harness that executes one selected native function, dispatches one
omitted function through the interpreter, and verifies interpreted return plus native
reentry with ARM/Thumb and full CPU-state checks. Only after that correctness gate should
selection expand beyond this reset-vector closure and memory accounting include the
linked runtime. No shared GBARecomp source change is authorized by this experiment.
