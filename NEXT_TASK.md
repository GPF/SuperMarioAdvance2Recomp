# Runtime SH-4 dynarec under nullDC3 — startup fixed, diagnostic PASS (emulator evidence only)

Two independent defects blocked the dynarec ELF; both are fixed without touching
GBARecomp, KOS, the compiler, or nullDC3.

1. **Constructor crash (root cause).** The merged GBARecomp runtime added
   `thread_local` objects (`g_runtime_tail_arg`, `g_runtime_native_ram_base`, the
   `ram_entry` static in `runtime_arm.cpp:974`, `scratch` in `gba_bus.cpp:643`),
   so the linker emitted a 64-byte `PT_TLS` header whose `.tbss` sits at
   `0x8c197d4c`, the same address as `.ctors`. In the ELF itself this overlap is
   legitimate (NOBITS TLS template, filesz 0) and the on-disk `.ctors` table is
   valid (`-1`, ten `__GLOBAL__sub_I_*` entries, `0`). nullDC3's REIOS loader
   (`libswirl/reios/reios_elf.cpp:44-71`, measured by reading source) copies and
   zero-fills **every** program header regardless of `p_type`, so `PT_TLS`
   memset 0x40 bytes over the freshly copied `.ctors`; `___do_global_ctors_aux`
   then called address 0 (PC `0x00000002`). The pre-merge control ELF
   (`ce17c68f…54c4c2d`) has no `PT_TLS`. Inference: real dcload/KOS loaders
   honour `p_type`, so hardware was likely unaffected, but removing TLS is
   correct for the single-threaded Dreamcast runner either way.
   **Fix:** `apply_no_tls()` in `tools/prepare_dreamcast_framework.py` strips
   the TLS qualifier in the staged overlay copy only.
2. **Harness staging.** `tools/run_nulldc3_dynarec_diagnostic.py` staged assets
   as symlinks, but nullDC3's `/pc` opens with `RESOLVE_NO_SYMLINKS`
   (`dcload_fs.cpp:135`), so `game.toml`, BIOS and ROM were invisible and an
   uncaught `std::filesystem_error` aborted. It now stages hash-checked copies.

Result (run dir `/tmp/sma2-dynarec-nulldc3-tlsfix-20261020-run2`, evidence in
`docs/results/evidence/sma2_nulldc3_dynarec_tlsfix_20261020/`): ELF
`/tmp/sma2-dynarec-tlsfix-dc-20261020/SuperMarioWorldRecomp.elf`
SHA-256 `41b2051ea2a05feb084bdca8c6dd6718a1161a110cc2fe39ad3ac057bb1faab3`;
strip-debug derivative loaded by REIOS
`347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5`; 3 program
headers, no `PT_TLS`. Log shows `[sma2] main entered`, backend
`sh4-dynarec`, `[sma2-jit] backend init succeeded`, and stage1/2/3/D/E each
PASS with 1 native block, 0 fallback, 0 mismatches, CPSR/IWRAM match and
native==interpreter cycles. Runner verdict `guest_diagnostic_verdict=PASS`,
emulator status 0, `final_summary_seen=true`. This is the runtime dynarec path
(empty static dispatch table, so static AOT could not have run); it covers
five tiny blocks, not gameplay. Guest `main()` return status is not exposed by
nullDC3. The 343-entry AOT corpus and its hashes were not touched. Host:
`py_compile`, ppu-timing test, asset-fallback test, `git diff --check` pass;
`test_sma2_cart_aot_diag_result.py` needs a probe binary and was not run.
Remaining: no gameplay-length dynarec run, no hardware validation (none
launched), EWRAM not in the JIT comparison, GBARecomp working tree still
holds uncommitted state this build depends on.

# Runtime SH-4 dynarec under nullDC3 — HOLD

The recovered bounded AOT diagnostic remains accepted only for its single
generated Thumb `BX LR` entry (`0x0809DFBE`): physical Dreamcast executed one
native entry, native/interpreter cycles were 11/11, compared state matched,
`SMA2_BOUNDED_AOT_DIAG=PASS`, and the program returned 0. Preserve the exact
343-entry corpus and its provenance.

Runtime SH-4 dynarec validation is **NO-GO**. The isolated build linked with
`SMA2_SH4_DYNAREC=ON`, but nullDC3 under REIOS faulted in
`___do_global_ctors_aux` at PC `0x00000002` before SMA2 `main()`, backend init,
or any JIT block. The fresh-run harness and hashes are recorded in
[`SMA2_BOUNDED_AOT_RECOVERY_AND_DYNAREC_20261020.md`](docs/results/SMA2_BOUNDED_AOT_RECOVERY_AND_DYNAREC_20261020.md).
Next work: determine why this ELF fails during REIOS/KOS constructor startup,
then rerun the same isolated dynarec diagnostic. Do not use the static AOT leaf
as a substitute for runtime dynarec evidence. Do not launch physical hardware
without separate authorization.

# Current SMA2 renderer handoff — frame-201 pixel gates CLOSED

The nullDC3 frame-201 all-native and mixed-row pixel-comparison gates are
CLOSED. Physical Dreamcast acceptance remains authoritative: native frame 201
passed at 160 native / 0 fallback rows and 0/38,400 raw RGB565 mismatches;
mixed frame 201 passed at 152 native / 8 fallback rows and 0/38,400 raw
mismatches. nullDC3 passed 0/38,400 only after the documented ownership-based
emulator color model. Do not apply its RGB565 fallback model to native
ARGB1555 palette rows. Full CPU/device/IRQ/clock fingerprint equivalence is
still incomplete. Details and reusable offline validation:
[`SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md`](docs/results/SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md),
[`SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md`](docs/results/SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md),
[`SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md`](docs/results/SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md).

## Next task: validate native PVR on grid checkpoint g_599

Use the existing default-OFF SMA2 native-PVR diagnostic path and
`docs/results/evidence/sma2_iwram_aot_invalidation_20261019/normal_gameplay_benchmark/music_scenes/grid_ckpt/g_599.gbas`
(SHA-256 `146dc1b989598c5505d46c1cf84377e52b6f5b3e0b94a8db53d4ec58e6af9a77`).
The host probe reports 160/160 eligible rows, but this is only an upper bound.
First verify checkpoint/input hashes and restore behavior, determine the first
completed guest-frame number from runtime evidence, and establish a same-run
software oracle. Keep software composition enabled and IWRAM AOT/native-skip
disabled for this correctness gate. Do not reuse a saved PNG or unrelated
frame as the oracle.

Request a separately authorized, single bounded Dreamcast run only after
offline preflight succeeds. Acceptance: native rows actually execute; all 160
rows have explicit native/fallback ownership and reasons; guest state matches
the same-run reference at the selected boundaries; PVR completion and stable
front/back ownership pass; and the displayed 240x160 RGB565 buffer matches the
same-frame software oracle at exactly 0/38,400 raw pixels. Stop on the first
restore, state, lifecycle, ownership, or pixel failure. Keep renderer defaults
unchanged. Consider `g_999` only after this checkpoint passes. Preserve the
full guest CPU/device/IRQ/clock fingerprint gap as a separate limitation and
make no performance claim from correctness runs. Eligibility evidence:
[`SMA2_NATIVE_ELIGIBILITY_AND_CAPTURE_ORDER_20261019.md`](docs/results/SMA2_NATIVE_ELIGIBILITY_AND_CAPTURE_ORDER_20261019.md).

---

## Preserved historical handoffs (everything below this line)

Added 2026-10-19 during the repository preservation phase. The sections below are
earlier handoffs, preserved verbatim from the working tree; several still carry
"Current"/"Latest" in their titles because that was true on their date, and none
is the current task (the current handoff is the PVR section above). Their recorded
statuses stand and must not be read as promoted: HOLD, CONDITIONAL GO, NO-GO,
FAIL, NULL and default-OFF experiments stay exactly as labelled. Nothing in this
file declares SH-4 AOT, relocated-IWRAM AOT, the PVR bridge, LZ4/SH-4, or the
I-cache/PMU work production-ready. New handoffs go at the top; do not overwrite or
truncate this file (see AGENTS.md, repository preservation).


# Current Handoff: R1-S (ld-only + `-S`) — link succeeded, runtime image NOT identical => **FAIL** (2026-10-17)

Addendum in `docs/results/SMA2_ICACHE_R1_RELOCATION_20261017.md`; artifacts `/home/gpf/sma2-r1s-ldonly-20261017/`. One link, rc=0, ELF 3d6cedad...: entry same, but all 3 PT_LOAD sha256 differ:
`.sma2pmu_text` (0x3a0 B, native section of sma2_window_pmu.cpp.obj) missing, .text -0x18, .ctors -4 B (data shifted 4), symbols `__stack_chk_init/__stack_chk_guard/sma2pmu_get_ticks` absent, ~630 symbols moved.
Cause (inferred): ld-only command omitted the ORIGINAL objects' native sections that plugin-mode linking keeps. -S fixed the debug-placeholder errors. No retry; hot bytes/pools not compared (image already differs).
No ld-only identity baseline yet. Recommended (approval needed): read-only derivation from the reference map of which original inputs/sections contribute natively, then ONE further link. B16-PM/LS not built; G1-G7 not started; no hardware/commits.
Backup of previous NEXT_TASK.md: `~/nexttask-recovery/NEXT_TASK.pre-r1s-20261017.md` (sha256 c6c5383c...).

Gate 9D runtime validation: **CONDITIONAL** (report: `docs/results/SMA2_GATE9D_RUNTIME_VALIDATION_20261017.md`; matched captures: `docs/results/evidence/sma2_gate9d_runtime_ab_20261017/`). Matched verbose-OFF NullDC3 comparison PASS: 272 miss calls / 176 unique misses; original performed 176 lookups / 71 formats, treated performed 0 / 0; all 272 guest snapshots, endpoint, EWRAM/IWRAM/I/O and captured GbaIo state match. Native entries 0; fallback instructions 51. Actual verbose-ON sparse-map runtime output and physical Dreamcast validation remain outstanding. Next Gate 9D action: after separate hardware authorization, validate both diagnostic modes on target. Keep this separate from the active R1-S investigation above. No hardware or commit.

---

# Current Handoff: R1 — load image PASS, relocation inventory done, ld-only identity relink **FAIL** (2026-10-17)

Report: `docs/results/SMA2_ICACHE_R1_RELOCATION_20261017.md`. Scratch ELF 2e4d3615... vs reference f0e71d64...: entry + all 3 PT_LOAD (addr/size/flags/align/file-backed sha256, bss extent) IDENTICAL (tools/research/icache_layout/loadimage_cmp.py); R0 strict file-hash FAIL stands.
ltrans inventory (15 objs, 910 `.text.*` sections, align 4): only R_SH_DIR32 (text) and R_SH_REL32 (.eh_frame only); NO PC-relative inter-section relocations, pools live in the owning section => hot functions are independently movable (earlier "34 external branches" were objdump pool artefacts).
Single ld-only relink (R1_ld.sh, no gcc/lto) produced NO ELF: 46,144 undefined refs, all in .debug_info (3,837 plugin-supplied debug placeholder symbols from 218 *.debug.temp.o). No retry.
DISCLOSURE: R0 -save-temps left 163 `*.debug.temp.o` files in /opt/toolchains/dc/kos/lib/dreamcast (my byproduct, not deleted; cleanup needs approval).
Next (approval needed): ONE more ld-only link with `-S` (or with the retained debug.temp.o); PASS = load-image + symbols equal to reference. B16-PM/LS not built; G1-G7 not started. No hardware/commits.
Backup of previous NEXT_TASK.md: `~/nexttask-recovery/NEXT_TASK.pre-r1-20261017.md` (sha256 799c14fd...).

---

# Current Handoff: scratch LTO re-link — R0 **FAIL (strict)**, R1 not reached (2026-10-17)

Report: `docs/results/SMA2_ICACHE_R0_RELINK_20261017.md`. One re-link done in scratch (original link.txt + `-save-temps -v`), ELF 2e4d3615... vs reference f0e71d64...:
all alloc sections, symbols, program headers IDENTICAL; only DWARF (.debug_info 730 B, .debug_line 10,244 B, .debug_line_str +368 B = link-time cwd) differ, so file hash differs.
No retry. 15 ltrans objects preserved in `/home/gpf/sma2-r0-relink-20261017/` (with SHA256SUMS). Preserved tree/ELFs/GAP re-verified unchanged. No hardware, no commits.
Needs decision: (A) relax R0 to allocated-content identity and run R1 (relocation inventory + ld-only identity relink), or (B) strict second re-link with cwd forced to /tmp/sma2-lowpmu-B16 (namespace/mount, tree untouched).
Backup of previous NEXT_TASK.md: `~/nexttask-recovery/NEXT_TASK.pre-r0-20261017.md` (sha256 d95014fd...).

---

# Current Handoff: B16-PM/LS G0 feasibility — **BLOCKED** (offline, 2026-10-17)

Report: `docs/results/SMA2_ICACHE_G0_FEASIBILITY_20261017.md`. Tools: `tools/research/icache_layout/` (README + setup_workdir.sh; reproduces 1,555/111 pairwise numbers).
B16's hot code exists only as transient LTO output (`/tmp/ccXXXX.ltransN.ltrans.o`, deleted); inputs are fat-LTO objects. A no-LTO link-only attempt of the preserved
build tree (`/tmp/sma2-lowpmu-B16`, ELF f0e71d64... = reference, unchanged) gave a different program (.text 0x12bf38 vs 0xf8ca4, 0/2006 common symbols at the same address, no runtime_tick.part.0) => identity FAIL.
Reference ELF/map hashes (B16, L0B16, B16-GAP) re-verified OK. No GCC/LTO run, no hardware, no commit. G1-G7 not started; no candidates built.
Negative B16-PM would reject only the tested placement map, not all placement explanations. Residual confounders listed in the report (tick geometry, pool D-sets, stack phase, executed-line unknowns).
Smallest next step (needs approval): ONE LTO re-link of the preserved build tree in scratch with ltrans temps saved, must reproduce f0e71d64... byte-identically; then ld-only relink from kept objects + `readelf -r` relocation inventory. Post-link patching = separate unapproved approach.
Backup of previous NEXT_TASK.md: `~/nexttask-recovery/NEXT_TASK.pre-g0-20261017.md` (sha256 528d4612...).

---

# Current Handoff: I-cache regression ROOT-CAUSE (offline, 2026-10-17) — one experiment proposed, awaiting approval

Offline only (no hardware, builds, source or git changes). Report: `docs/results/SMA2_ICACHE_ROOT_CAUSE_OFFLINE_20261017.md`.
Findings: of the hot path only `runtime_tick` (B16 split 2,264+44 B, bridge calls `.part.0` directly -> L0B16 merged 2,576 B, bridge calls it; frame 68->80 B, +~10 insn/call),
`retire_impl` (+12 B), bridge (-28 B), `GbaBus::read8` and `ParityBus::write8` changed; every other hot function (step, decoders, dispatch_resolve, lookup_in, `_Rb_tree`, bus helpers...)
is byte-identical modulo pointers (2,193 compared) yet >=25 hot-pair I-index overlaps change (bridge x tick 9->75, tick x `_Rb_tree` 0->43, retire x ThumbDecode 0->29).
Codegen growth cannot supply +346 k misses (<=~33 k, <=9.5 %) and explains only ~31 % of the +535 k issued instructions (rest unexplained). Operand-cache pool/hot-data alias counts are
at chance level in both builds (B16 14, L0B16 16, chance 10.7+-4.1 / 13.1+-4.9); the I-miss COUNT cannot depend on data placement. Most likely: non-uniform relative I-placement of
code-identical hot functions (moderate-low confidence, ~55 %); a static placement model gives L0B16/B16 anywhere 0.96-1.33 depending on the unknown executed footprint, so it is NOT proven.
Record correction: the three B16-GAP comparability failures were NOT in the same rotation order (pair-diffs.csv); they share block index 02 (B16 slow + L0B16 fast).
**Proposed (needs approval; link-only build + ~52 launches):** B16-PM = B16 bytes re-linked at L0B16's I-cache index map (92.9 % of the pairwise change reproduced offline; residual = tick geometry),
vs layout-matched control B16-LS (hot fns moved by multiples of 16 KiB), B16, L0B16; events 0x08/0x24/0x25 (+0x13), Williams 4x4 order, decision f=(PM-B16)/(L0B16-B16) on 0x08
(H-P f>=0.8, H-C f<=0.2). Offline gates G0-G7 (G0 = link-only dry run proves hot `.text.<fn>` sections are placeable and an identity script reproduces B16) must pass first. Preferable if feasible: an SH-4 fetch trace + cache sim.
Backup of the previous NEXT_TASK.md: `~/nexttask-recovery/NEXT_TASK.pre-icache-20261017.md` (sha256 a8038ba3...e205dfa).

---

# Current Handoff: B16-GAP insertion-only experiment — NULL (layout translation does not reproduce the regression)

B16 + inert 13,664 B gap (all hot code shifted exactly +13,664 B = 171 I-cache lines, data +0x4000, instructions identical after shift): I-cache misses +153 (+0.015%)
vs L0B16 +346 k; I-freeze +0.41 M (3.5% of L0B16's 11.69 M); time -5..-9 ms vs B16 (L0B16 +65..+69 ms). 46/46 launches valid; 3 non-consecutive
control-pair comparability misses (block 02, rotation order L0B16,GAP,B16). So the B16->L0B16 effect is not uniform translation: remaining = compiler-generated
bridge/runtime_tick differences and/or non-uniform relative layout (not separable yet). No follow-up started.
Report: `docs/results/SMA2_B16_GAP_ICACHE_EXPERIMENT_20261017.md`. (The LZ4/SH4ZAM task was interrupted earlier and NOT started.)

---

---

# Current Handoff: LOWPMU follow-up events — 30/30 launches OK, 0 comparability failures

I-cache misses +346,146 +- 170 (+34.7%); D-cache stall cycles +3.44 M +- 1.17 M (noisy); event 0x13 is INSTRUCTIONS ISSUED
(+535 k, +1.9%), not D-cache misses (prompt mislabel; no D-miss count collected; 0x0f would need authorization). Strengthens the I-cache
hypothesis; cannot yet separate placement vs code generation. Original Gate D failure (0x24 block 05) stands. Next (not implemented): insertion-only
control, B16 + inert 13,664 B gap at the generated-code slot. Report: `docs/results/SMA2_LOWPMU_FOLLOWUP_EVENTS_20261017.md`.

---

# Current Handoff: low-perturbation PMU — Gates A,B,C PASS; Gate D STOPPED at 0x24 block 05 (+55.1 ms < +57)

Wrapper approach (no change to any original function; `.text` byte-identical, 1 literal word patched in a copy) works on
hardware; penalty reproduced (Gate B +63..+75 ms). I-cache freeze (0x24) L0B16-B16 = 11.696 M cycles +-5 k every pair,
~84% of the elapsed-cycle penalty. 0x08/0x25/0x13 NOT run (stop rule). Next (needs approval): run remaining events with the same
`*-LOWPMU-sel.elf` (only /pc/sma2_pmu.txt changes), then a fixed-code relative-layout control.
Reports: `docs/results/SMA2_LOW_PERTURBATION_PMU_BUILD_20261017.md`, `..._HARDWARE_20261017.md`.

---

# Current Handoff: window-PMU experiment CLOSED — P0 FAIL (diagnostic evidence preserved)

9 valid launches (event 0x24); instrumented L0B16-B16 penalty +51 ms (gate +57..+85), B16 ~30 ms slower than
uninstrumented. I-cache-freeze delta L0B16-B16 = 8.04 M cycles every block (68-96% of elapsed delta) = clue only.
Cause of benchmark change: PMU code inlined into per-instruction `retire_impl` (+324 B, new regs/frame) plus
uniform hot-code shift (+432 B B16 / +236 B L0B16) and +96 B data shift. Remaining modes NOT run.
Next (approval needed): lower-perturbation measurement (counters outside `retire_impl`) with a layout-only control.
Report: `docs/results/SMA2_PMU_WINDOW_P0_FAIL_AND_PERTURBATION_20261017.md`.

---

# Current Handoff: window-level PMU experiment BUILD complete (no hardware run)

12 ELFs (B16/L0B16/TAIL x events 0x24,0x08,0x25,0x13) built with opt-in `-DSMA2_WINDOW_PMU_MODE=<hex>`
(`src/snapshots/sma2_snapshot_h5_diag.cpp`; prints `SNAPSHOT_H5_PMU mode= event= elapsed_cycles=`).
Static gate P0 passed: bridge 3912/3884/3884 B, `runtime_tick` split vs single, `g_cpu` placement,
TAIL = uniform -0x2B84 of L0B16; code/data symbol layout identical across the four modes per config.
ELFs, maps, `SHA256SUMS`, `build_pmu.sh`: `docs/results/evidence/sma2_pmu_window_build_20261017/`.
Not done (needs approval): hardware runs; run-time gate P0 (instrumented L0-B16 penalty must reproduce).

---

# Current Handoff: runtime performance / native-execution architecture AUDIT — research only, awaiting approval

Read-only audit (no builds, hardware, source or git changes). No named dominant cost exists for B16 (best
config): the only zone profile is of C/P0 before batching (~59% of cycles were I/D-cache freeze). The ~75 ms
link penalty has **no supported mechanism**: B16->L0B16 changed bridge/`runtime_tick` code generation,
relative hot-code layout and hot-global data layout together; the TAIL control was a uniform -0x2B84 B
translation of all hot code (preserves every pairwise offset), so it never tested relative layout.
Further batching: diminishing (fit leaves ~20-25 ms of per-entry cost at cap 16) and correctness-blocked
(IRQ one window, SWI/HALT untested, `CLK_` 4-byte mismatch). Native boundary: no state copy, but generated
code still calls `runtime_should_yield`/`runtime_tick`/`bus_*` per instruction.
**Recommended next (needs approval):** window-level SH-4 PMU comparison B16 / L0B16 / L0B16-TAIL (events
0x24, 0x08, 0x25, 0x13; one per run; TAIL-vs-L0B16 as positive control), gated on the instrumented trio
reproducing the penalty. Then a B16 zone profile and a gameplay PC/time profile before any decode-cache,
idle-recognition or AOT-expansion work.
Report: `docs/results/SMA2_RUNTIME_PERFORMANCE_ARCHITECTURE_AUDIT_20261017.md`.

---

# Current Handoff: KOS KLF dynamic-AOT feasibility — CONDITIONAL GO (technical), benefit unproven

Read-only study, nothing built or run. KOS `.klf` is an `ET_REL` file loaded by `elf_load`, which
supports **only `R_SH_DIR32` RELA** (`kernel/fs/elf.c:266`). The existing generated object has two
`R_SH_REL32` in `.eh_frame`, so the module needs `-fno-unwind-tables -fno-asynchronous-unwind-tables`
and `-fno-lto` at compile time. The dispatch table is `const`/link-time (`runtime_arm.cpp:40-41`), so a
mutable table, an export TU and `INIT_EXPORT` must go into the main ELF; an identical-B16 ELF is not
achievable and the fair control is a new "B16-K" (loader, no module). Whether this removes the ~75 ms
is unknown. Next (needs approval): build-only Gate 0 (`.klf` relocation check) and Gate 1 (B16 vs
B16-K hot-function bytes), and the same Gate 1 for static `-fno-lto`. No hardware.
Report: `docs/results/SMA2_KOS_KLF_AOT_FEASIBILITY_20261017.md`.

---

# Current Handoff: AOT tail-placement hardware test — PASS correctness, REGRESSION timing

The preserved L0B16-TAIL ELF passed one locked 16,384-instruction smoke test:
endpoint byte-identical to CONTROL, native=0, launcher exit 0, and lock released.
Ten rotating matched B16/L0B16/TAIL blocks also passed those criteria. Mean times:
B16 366.469 ± 5.011 ms; L0B16 440.908 ± 5.797 ms; TAIL 446.860 ± 4.079 ms.
TAIL was 5.952 ms slower than L0B16 in paired differences and recovered none of the
linking penalty. Do not promote this placement or begin a placement sweep.

Full report: `docs/results/SMA2_AOT_TAIL_HARDWARE_20261017.md`.
Raw logs, run manifests, endpoint comparisons and hashes:
`docs/results/evidence/sma2_aot_tail_hardware_20261017/`.
No PMU test or additional layout variant was run. The result does not prove an
instruction-cache cause; B16 and linked variants retain the established CLASS B
code-generation differences.

---

# Previous Handoff: AOT bridge batching — correct through cap 16, B16 369 ms vs C 423 ms; A2 matched-layout measured

## Bridge batching / state-copy investigation — 2026-10-17

The bridge did 5 full CPU-state copies per instruction (not 3). Opt-in switches in
tools/prepare_dreamcast_framework.py (SMA2_BRIDGE_DEFER_COPY, SMA2_BRIDGE_BATCH=N, default OFF)
defer 3 of them and batch up to N instructions per top-level bridge entry, re-implementing the
runner/dispatch checks between instructions (native-entry, halt, VBlank, H5 stop, IRQ/SWI exit;
nested bridges stay cap 1). Hardware: 25/25 configs (caps 1-16, with/without the two native
functions, 256-16,384) byte-identical to CONTROL, PC sequences identical, native 14/506
unchanged; a real-IRQ window is byte-identical to the unbatched bridge. Time (7 reps, 16,384 insns):
C 423.1, P1 652.9, D1 604.4, B2 515.4, B4 436.5, B8 394.0, B16 369.1 ms (deferral is independently
-48.5 ms at cap 1 and -65.4 ms at cap 16). Pre-existing, unexplained: the unbatched bridge
differs from C by 4 bytes (CLK_) after an IRQ is delivered.

After a console reboot the A2 set completed (49 clean launches, endpoints identical): with batching,
A2 beats its layout-matched control by 23.1 ms (423.1 vs 446.2), but linking the generated code costs
~75 ms, so A2B16 is slower than B16 with no generated code (369.1). Batching placement
sensitivity is small (+-1 ms). Failed wedged-console launches are preserved in
docs/results/evidence/sma2_aot_bridge_batching_20261017/failed-console-wedged/.
Next: control generated-code placement; SWI/HALT fixtures; host harness for batch exit rules; the
pre-existing CLK_ difference after IRQ. Report: docs/results/SMA2_AOT_BRIDGE_BATCHING_20261017.md.

---

# Current Handoff: AOT interpreter-bridge performance — P0->P1 -15.1%, correctness PASS

## Bridge performance investigation — 2026-10-17

Static-recompiled bridge mode (P0) was 761.6 ms vs 418.7 ms for the original
interpreter (C) on the 16,384-instruction post-IRQ window. Hardware ablation
found the largest single cost: `runtime_dispatch_miss` called
`std::getenv("GBARECOMP_STRICT_STATIC")` on every miss (one per guest
instruction in the parity bridge). Memoizing it (opt-in
`SMA2_BRIDGE_FAST_MISS=ON`, default OFF, title-local overlay patch in
`tools/prepare_dreamcast_framework.py`) gives **P1 = 646.9 ms (-114.7 ms, -15.1%,
SE 2.8 ms, 7 interleaved repeats, stable under 48 B / 2 KB layout shifts)**.
P1 and A2 pass the full differential at 256/1,024/4,096/16,384: byte-identical
endpoint images, identical retired-PC sequences, identical cycles, A2 native=506.
Host CTest 53/53. Remaining gap vs C: +228 ms (+54.5%), i.e. 66.5% of the
original gap; characterised (per-instruction dispatch+miss+bridge entry/exit
forced by the one-instruction parity cap; ~63% of the profiled increase is
I/D-cache pipeline stall) but NOT ablation-verified. Profiler (`SMA2_BRIDGE_PROF`,
PRFC0/PRFC1) perturbs small cold zones; use ablation for wall time.

Important confound: linking the generated functions costs ~+61 ms (layout /
I-cache conflicts, zero native execution). Against a layout-matched control,
A2's 506 native instructions save 35.3 ms (4.9%); against the unlinked P1 it
looks 38 ms slower. Report and evidence:
`docs/results/SMA2_AOT_BRIDGE_PERFORMANCE_20261017.md`,
`docs/results/evidence/sma2_aot_bridge_performance_20261017/`.

Next: (1) control the link placement of generated code and re-measure A2;
(2) measure a multi-instruction bridge (cap>1, safe boundaries preserved) to get
the real fallback cost before any bridge redesign. No third function, no idle-loop
work yet. Production `GBARECOMP_FORCE_INTERP=1` unchanged.

---

# Current Handoff: two-function SH-4 AOT differential — PASS

## Two-function post-IRQ AOT differential and coverage — PASS — 2026-10-16

`gf_tfunc_0809B4C8` plus `gf_tfunc_080740EC` (an OAM-attribute loop, 4 entries x
123 guest instructions) ran natively on physical Dreamcast from the unchanged
post-IRQ checkpoint. AOT-2 endpoints are byte-identical whole files to
interpreter CONTROL (and AOT-1) at 256, 1,024, 4,096 and 16,384 retired
instructions, and the retired-PC sequence equals CONTROL's over all 16,384
(frame 200->201 crossed). The earlier B4C8+D77C NO-GO was a diagnostic
double-count of a post-BL bookkeeping tick, not an architectural divergence;
fixed in `instrument_native_v2.py`.

Coverage at 16,384: 506 native (3.09%), 21.91% of the 2,309 non-idle
instructions (AOT-1: 14, 0.61%). 85.9% of the window is the idle IWRAM flag poll
`0x0800052E-0x08000534`. Raw generated SH-4: 10,192 B for the pair.
Timing (5 interleaved hardware repeats): AOT-2 761.9 ms vs 761.2 ms for the same
bridge mode with zero generated functions; static-recompiled mode itself is
+83% over interpreter-forced CONTROL. **No speedup is claimed.** No IRQ/SWI was
delivered in this window; AOT-2 IRQ/SWI continuation is untested. 26 locked
hardware launches, all clean. Host CTest 53/53; the host game executable still
fails to link (missing `kDispatchTable`, unchanged).

Report and evidence: `docs/results/SMA2_AOT_TWO_FUNCTION_COVERAGE_20261016.md`,
`docs/results/evidence/sma2_aot_two_function_20261016/`.

Next: do not add arbitrary functions. Reduce bridge-mode overhead and decide
how to treat the idle poll (separately, explicitly idle-classified), then
re-measure on a deterministic multi-frame workload where non-idle work exceeds
~14%. Keep production `GBARECOMP_FORCE_INTERP=1`.

---

## Previous: extended one-function differential — PASS — 2026-10-16

## Extended post-IRQ one-function differential — PASS — 2026-10-16

Using the unchanged post-IRQ checkpoint and the same one-row static table
`{0x0809B4C8, Thumb} → gf_tfunc_0809B4C8`, CONTROL and AOT matched at 256,
1,024, 4,096, and 16,384 retired guest instructions on physical Dreamcast.
The portable `CPU0`, `CALL`, `IRQ_`, `CLK_`, and `DEV_` endpoint sections
matched byte-for-byte at all four boundaries, including the 16,384-instruction
run that advanced frame 200→201. All eight launches exited cleanly and released
the hardware lock.

The AOT path executed 14 guest instructions in the selected generated SH-4
function per run; the rest used the interpreter bridge: 242/14 at 256,
1,010/14 at 1,024, 4,082/14 at 4,096, and 16,370/14 at 16,384
(interpreter/native). This confirms the bridge bookkeeping correction across
longer continuations and a frame transition. It also shows current native
coverage is small: 0.085% at 16,384 instructions. Do not add more generated
functions until the next profile identifies a reached, eligible candidate.

Host CTest passed 53/53. Production interpreter forcing remains enabled, and
only the existing single generated row was linked. Full hashes, raw logs,
endpoint images, counters, and comparison outputs:
[`docs/results/SMA2_AOT_EXTENDED_DIFFERENTIAL_20261016.md`](docs/results/SMA2_AOT_EXTENDED_DIFFERENTIAL_20261016.md).

Next: profile the established 16,384-instruction continuation for the next
eligible non-idle generated function, then add candidates gradually and rerun
the same CONTROL/AOT differential. Do not link the full generated image.

## Earlier AOT mode-parity and native-entry result — PASS — 2026-10-16

## Latest AOT mode-parity and native-entry result — PASS — 2026-10-16

CONTROL and static-recompiled interpreter mode now match from the same
post-IRQ checkpoint at 1, 16, 64, 256, 1,024, and 16,384 retired guest
instructions. `CPU0`, `CALL`, `IRQ_`, `CLK_`, and `DEV_` endpoint sections are
byte-identical at every final pair. The mode-specific bridge previously
bypassed runtime bus bookkeeping and failed to count bridge instructions at
H5 boundaries; the title-local diagnostic overlay now routes those accesses
through the same bus helpers and counts each bridge retirement. Host CTest is
53/53 PASS.

After parity passed, the one-row static dispatch configuration entered
`gf_tfunc_0809B4C8` on physical Dreamcast. The 64-retirement AOT endpoint
matched the interpreter CONTROL endpoint byte-for-byte, including device and
scheduler state. This is `SMA2_AOT_SINGLE_FUNCTION_DIFFERENTIAL_PASS`; the
production interpreter default remains enabled and no broad generated table
was linked. Full source analysis, endpoint hashes, ELF hashes, raw logs, and
the preserved pre-fix divergence are in
[`docs/results/SMA2_AOT_MODE_PARITY_20261016.md`](docs/results/SMA2_AOT_MODE_PARITY_20261016.md).

Next: preserve this passing one-function configuration and choose another
demonstrated, meaningful continuation entry only if it adds useful coverage.
Keep production AOT disabled; do not expand to a full generated table.

## Previous checkpoint milestone — post-IRQ safe capture and non-idle profile — PASS — 2026-10-16

## Latest checkpoint milestone — post-IRQ safe capture and non-idle profile — PASS — 2026-10-16

The frame-200 interpreter restore entered one additional IRQ (entry count
191→192), remained in that continuation for 8,192 deferred instructions, then
returned to a safe boundary. H5 captured a 635,929-byte checkpoint at guest PC
`0x0809B274` / next PC `0x0809B276`, Thumb, guest cycles 56,424,198, with
`irq_depth=iret_depth=0` and return depth/floor 0/0. This is
`SMA2_POST_IRQ_SAFE_CHECKPOINT_PASS`.

The same 76,016-instruction profile left the four-PC frame handoff poll and
recorded 13 executions through the non-idle Thumb block `0x0809E510–0x0809E520`,
mapped to `gf_tfunc_0809E510` in the existing generated corpus. This is
interpreter-only profile evidence; the histogram saturated and the corpus was
not linked into the hardware diagnostic ELF. Do not claim AOT execution or
eligibility yet.

Host snapshot regression passed 45/45. The diagnostic Dreamcast build was
interpreter-forced; the successful locked launch exited 0, printed
`Program returned 0`, and released its lock. Checkpoint hash:
`50282ea4be8bea13b9063a38438dcde0b0b330bf0f3b929edf525bb112af32e6`.
Full commands, ELF/source inputs, logs, and limitations are in
[`docs/results/SMA2_POST_IRQ_SAFE_CHECKPOINT_20261016.md`](docs/results/SMA2_POST_IRQ_SAFE_CHECKPOINT_20261016.md).

Next: profile a bounded continuation from this checkpoint to find a complete,
eligible non-idle generated function after the IRQ return. Then build a
one-function opt-in SH-4 diagnostic and run a same-checkpoint interpreter/AOT
differential. Keep production AOT OFF and preserve fallback.

## Historical AOT checkpoint candidate gate — superseded by parity result

## Earlier checkpoint-only continuation gate — 2026-10-16

The frame-200 x86-32 checkpoint still restores and the 16,384-instruction
interpreter continuation remains an exact PC/Dreamcast reference. A fixed-size
on-hardware histogram shows exactly four Thumb PCs, each retired 4,096 times;
they all map to the generated body `gf_tfunc_0800052E`, a frame-flag poll.
That idle loop is not an acceptable sole AOT candidate. A bounded
65,536-instruction interpreter control reached an active IRQ/SWI continuation
at the requested endpoint, so the snapshot diagnostic correctly refused to
capture it. The run exited cleanly, but no safe longer endpoint or meaningful
next function was established. No AOT hardware ELF was launched. Evidence and
the precise gate stop are in
[`docs/results/SMA2_AOT_FROM_CHECKPOINT_20261016.md`](docs/results/SMA2_AOT_FROM_CHECKPOINT_20261016.md).

Next: obtain a safe checkpoint after legitimate event/IRQ return and guest
progress beyond the frame poll, then re-profile for a meaningful generated
function. Preserve interpreter-forced production behavior.

## Latest result — PC polling-loop repair and frame-200 checkpoint — 2026-10-16

The x86-32 cold-boot differential harness had omitted the separate `GbaIo`
bus/PPU links, so immediate DMA left DMA3 enable set forever at the guest wait
at `0x0809E1E6`. The harness now wires them as application initialization
does. PC execution then reached the normal per-frame IWRAM handoff at
`0x0800052E–0x08000534`, and a real frame-200 level-scene checkpoint was
captured. Its x86-32 restore and physical Dreamcast interpreter continuations
matched at 64, 1,024, and 16,384 instructions. Three Dreamcast launches
completed with clean exits and released locks. This is a useful AOT
investigation checkpoint; controllable gameplay fast-start is still NOT
DEMONSTRATED. Full root-cause evidence, hashes, and logs:
[`docs/results/SMA2_PC_GAMEPLAY_CHECKPOINT_BRINGUP_20261016.md`](docs/results/SMA2_PC_GAMEPLAY_CHECKPOINT_BRINGUP_20261016.md).

Next: continue selective interpreter-vs-SH-4 AOT investigation from the
frame-200 checkpoint, while keeping production AOT disabled. A future
gameplay fast-start milestone needs a deterministic input path that reaches a
controllable game state.

## Latest result — PC-to-Dreamcast checkpoint portability — 2026-10-16

`SNAPSHOT_PC_DC_DIFFERENTIAL_PASS` is established for a genuine x86-32 host
interpreter checkpoint restored by physical Dreamcast hardware. Immediate
state and 64, 1,024, and 16,384 retired-instruction endpoints match across
portable `CPU0`, `CALL`, `IRQ_`, `CLK_`, and `DEV_` sections. Only the optional
host runner counters in `META` differ; the Dreamcast H5 diagnostic documents
and excludes those counters. Full hashes, commands, logs, comparisons, and
remaining limitations are in
[`docs/results/SMA2_SNAPSHOT_PC_TO_DC_20261016.md`](docs/results/SMA2_SNAPSHOT_PC_TO_DC_20261016.md).

The demonstrated checkpoint is from ROM polling-loop code at `0x0809e1e6`,
not a useful gameplay location. Thus gameplay fast-start and IRQ/timer/DMA event
continuation remain NOT DEMONSTRATED. Return to the bounded SMA2 AOT-from-
checkpoint integration plan, keeping production AOT disabled. A future
gameplay fast-start test needs a deterministic guest-visible input sequence.

The host portability producer must use the 32-bit x86 ABI (`-m32`); x86-64 is
correctly rejected by the current `size_t` profile check.

## Latest result — 2026-10-16

**`AOT_IRQ_CONTINUATION_HARDWARE_PASS` and
`AOT_IRQ_FALLBACK_REENTRY_PASS` on host and physical Dreamcast for the
synchronous, non-resumable IRQ path.** Interpreter startup reached SMA2's real
installed IRQ handler, then the bounded fixture entered generated Thumb code at
`0x08005538`, delivered one eligible Timer 0 IRQ through vector `0x18`, ran the
actual copied IWRAM handler through the interpreter bridge, acknowledged and
returned to `0x08005538`, and executed the next generated instruction. The 40
IRQ/BIOS instruction PCs, full CPU state, full IWRAM/EWRAM, device snapshot,
scheduler timing, and 2,144 cycles matched the interpreter. The Dreamcast
program returned normally. This proves synchronous guest-call-stack ownership;
it does **not** prove suspension/resumption of an attached `RuntimeArmContext`,
SWI continuation, or production AOT startup. Full evidence:
[`docs/results/SMA2_AOT_IRQ_CONTINUATION_20261016.md`](docs/results/SMA2_AOT_IRQ_CONTINUATION_20261016.md).

The ROM installed an initial BIOS IRQ pointer at `0x080000E0` (`0x080000FC`),
later replaced it at `0x08001654` with the actual IWRAM handler `0x03002364`;
the Timer 0 callback `0x080014B9` was stored at `0x0809DFC0`. HLE boot's zero
handler pointer is not used as the test state. Normal gameplay still forces
interpreter execution. Next, investigate SWI continuation and the unresolved
3 MiB heap-allocation warnings before any broader startup AOT experiment.

**`AOT_FALLBACK_REENTRY_PASS` for the bounded diagnostic.** Generated Thumb
functions at `0x08007056` and `0x08007058` passed a nine-instruction
host/Dreamcast differential. A second fixture passed generated → one-instruction
interpreter miss → generated reentry. Both return normally; production gameplay
still forces the interpreter. At that milestone IRQ/SWI continuation had not
yet been tested; the later synchronous IRQ result is recorded above. Full
evidence is in
[`docs/results/SMA2_AOT_MULTI_FUNCTION_PERF_20261016.md`](docs/results/SMA2_AOT_MULTI_FUNCTION_PERF_20261016.md).

The repeated matched hardware fixture completed 1,152 instructions/6,656 guest
cycles per backend. Both paths called `runtime_tick` and
`runtime_should_yield` 1,152 times. Mean measured path time was 5.266 ms native
versus 19.987 ms interpreter at the existing generated-source `-O1` setting.
`-Os` reduced whole-ELF `.text` by 7.62%, but its native fixture was 5.644 ms;
this does not establish an `-Os` speedup. Keep the default at `-O1`.

Latest bounded extension: the Reset-corpus path at Thumb `0x08005538` now
performs EWRAM byte reads, observable IWRAM writes, flag updates, and taken /
not-taken conditional branches before returning. Full CPU state, IWRAM/EWRAM,
cycles, and event counters match the interpreter on host and physical Dreamcast
under O1 and `-Os`. Evidence and exact hashes are in
[`docs/results/SMA2_AOT_MEMORY_BRANCH_EVENTS_20261016.md`](docs/results/SMA2_AOT_MEMORY_BRANCH_EVENTS_20261016.md).

The next fixture crossed a Timer 0 event horizon with IE/IME disabled and
CPSR.I set. Host and physical Dreamcast matched the timer IF transition at the
same guest boundary, complete serialized device snapshot, CPU state, and 44
cycles; the pending IRQ was not delivered. This establishes
`AOT_EVENT_FLUSH_HARDWARE_PASS` and `AOT_IRQ_INELIGIBLE_PASS`. That fixture
covered a pending but ineligible IRQ; eligible synchronous IRQ continuation is
recorded above. Full evidence: [`docs/results/SMA2_AOT_EVENT_CONTINUATION_20261016.md`](docs/results/SMA2_AOT_EVENT_CONTINUATION_20261016.md).

The eligible synchronous IRQ path is now separately validated above. The
resumable-context and SWI continuation contracts remain unproven; keep normal
production startup interpreter-forced.

## Earlier baseline: isolated generated SH-4 dispatch

**Verdict: `ISOLATED_AOT_HARDWARE_PASS` (Contract 1 only).** On 2026-10-16, a
bounded hardware diagnostic dispatched the Reset-corpus Thumb leaf at
`0x0809DFBE` through GBARecomp's ordinary static `runtime_dispatch` table. The
generated function executed one `BX LR`, returned to `0x080015FA`, and matched
the production interpreter in complete CPU state, 11 guest cycles, full IWRAM,
and full EWRAM. It recorded exactly one generated entry and returned cleanly
(`Program returned 0`). This establishes isolated generated-function execution,
not cart-startup execution or gameplay correctness. Full logs, hashes, and the
fixture constraints are in
[`docs/results/SMA2_ISOLATED_AOT_DISPATCH_20261016.md`](docs/results/SMA2_ISOLATED_AOT_DISPATCH_20261016.md).

The opt-in fixture is `SMA2_CART_AOT_DIAG=ON` and requires
`SMA2_CART_AOT=ON`. It runs once before the first guest step, calls the normal
static dispatcher for an exact generated entry, then runs the corresponding
instruction through `runtime_force_interp_step()`. `GBARECOMP_FORCE_INTERP=1`
remains enabled for the ordinary Dreamcast run loop; the diagnostic's explicit
`runtime_dispatch()` call bypasses that outer-loop choice without changing
production policy. The selected function is one instruction and has no nested
call, SWI, IRQ, MMIO, DMA, or loop. The diagnostic is OFF by default and rejects
missing table ownership, wrong opcode/mode, pre-existing IRQ/halt state, and
insufficient device-event headroom.

This does not resolve bounded production startup. A generated function can
yield at its per-instruction safe point, but the ordinary runner does not
preserve an arbitrary nested generated/interpreter/IRQ continuation. Keep the
default interpreter policy. Do not enable unrestricted generated cart
execution based on this isolated result.

`src/main.cpp:44-46` deliberately sets `GBARECOMP_FORCE_INTERP=1`. The
GBARecomp loop reads this at `src/runtime/runtime.cpp:1605-1610` and
`runtime.cpp:2165-2173`; it interprets mainline instructions instead of looking
them up in the cart table. `runtime_arm.cpp:1543-1552` makes IRQ instruction
execution follow the same choice. This policy was introduced because the
Dreamcast port was still establishing interpreter startup, video, and
controller behavior (`src/main.cpp:44-45`), not because GBARecomp lacked a
static compiler. SWIs still enter the BIOS vector through `runtime_swi()` and
`runtime_dispatch(0x08)` (`runtime_arm.cpp:1402-1405`), so “force interpreter”
does not mean every generated BIOS function is bypassed.

Generated cart functions share canonical `g_cpu`, place the current guest PC
before each instruction, call `runtime_should_yield()` before executing, and
charge instruction timing through `runtime_tick()`. The tick path handles
devices and IRQ delivery. VBlank yield can return from generated code, but
repeated yields do not impose a total invocation budget; inside an IRQ the
normal VBlank unwind is intentionally suppressed. Static table lookup remains
owned by `dispatch_resolve()`: exact PC/mode table match, then native overlay,
then the established interpreter miss bridge. The interpreter fallback uses
the canonical CPU and the generated guest-call return contract. These facts
establish the normal dispatch boundary, but do not prove SMA2 generated-code
correctness.

## Measured bounded corpora

The reproducible profiles are in `tools/sma2_corpus_profiles/`. Each retains
the ARM reset entry, disables broad AOT scan, speculative literal harvesting,
and automatic resume seeding, and lets FunctionFinder discover complete CFGs
for its roots. `FunctionFinder::run(max_functions)` treats manual seeds as
required roots; therefore the full `game.toml` (281 manual seeds plus broad
scan/literal/resume options) produced 23,037 functions with `--max-functions
16`. That is not a hard cap on required seeds.

| Profile | Extra reviewed roots | Functions | Dispatch rows | Generated C++ | SH-4 `.text` | ELF file | ELF SHA-256 |
|---|---:|---:|---:|---:|---:|---:|---|
| reset | 0 | 343 | 343 | 2,130,133 B | 1,602,769 B | 15,136,716 B | `342a6ac2ecd40ef738f74597e28eaff9c73c858854cd4d8e1c561b156e4603d4` |
| opening12 | 12 | 875 | 881 | 5,650,772 B | 2,247,761 B | 17,387,772 B | `409d226865dd2483e17e517c9afce76d412509e4cc1a864ca8074d1a1c96bb8e` |
| opening100 | 100 | 1,135 | 1,167 | 9,208,529 B | 2,842,281 B | 18,927,820 B | `28468132a7f9dc86482813ce3c4c2e8f284dbb494279c96d3820f7f27ec57e71` |

The fresh empty-table ELF is `1,150,717 B` `.text`, `8,176 B` `.data`, and
`1,146,048 B` `.bss`, file size `12,487,172 B`, SHA-256
`a98e2f21ecb854c7312016ac20ed0e56aac33200fa61409f8e626c067561583c`. Text
growth versus empty dispatch is +452,052 B, +1,097,044 B, and +1,691,564 B
for the three profiles. These ELFs link the generated table but still force
the interpreter; they are not execution results. See
[`docs/DREAMCAST_MEMORY_BUDGET.md`](docs/DREAMCAST_MEMORY_BUDGET.md) for loaded
segment and memory limits.

The two-function chain and non-resumable interpreter miss/reentry are now
covered by the latest result above. The remaining continuation investigation
still concerns resumable IRQ/SWI paths; do not infer those semantics from the
isolated fixture.

Historical direct Stage B hardware evidence and JIT Stage A–E records remain
below for reference.

## Older handoff: bounded first-divergence JIT prototype

## AOT milestone — Stage B passed on Dreamcast; gameplay remains NO-GO

An opt-in strategy C proof of concept reuses GBARecomp's offline
`FunctionFinder`, Thumb decoder, and semantic C emitter, compiles the selected
Stage B block for SH-4, and links it into a dedicated diagnostic ELF. The
generator validates the local ROM SHA-256 and exact block halfwords. The AOT
entry is called directly with no runtime JIT fallback; its bounded diagnostic
compares the result with the interpreter and terminates. Interpreter and JIT
builds remain available and the option defaults OFF.

Static object/ELF evidence, hashes, limitations, and the hardware record are in
[the AOT architecture note](docs/SMA2_SH4_AOT_BACKEND.md). The corrected ELF
passed one Stage B native/interpreter comparison on Dreamcast: 7 instructions,
43 cycles each, matching CPU and full IWRAM, matching branch/banked state, and
zero fallback. The program returned normally and released the shared lock. A
same-value store means the run did not independently prove a physical bus
write. The opt-in ELF now runs its bounded diagnostic automatically and
supports `/pc/sma2_exit.txt` for clean shutdown. Broader SMA2 AOT and gameplay
remain unvalidated. Historical Stage A–E JIT PASS records and the separate
four-block dynarec handoff below are unchanged.

## Historical handoff — four-block JIT boundary prototype (not current work)

Stages A–E have each passed their selected interpreter/JIT comparison on a
physical Dreamcast run. A–E are five checkpoints, not a gameplay correctness
percentage:

| Stage | Entry | Exit | Interpreter | Cycles |
|---|---|---|---:|---:|
| A | `0x080015D8` | `0x0809DFBC` | 10 instructions | 58 |
| B | `0x080015EC` | `0x0809DFBC` | 7 instructions | 43 |
| C | `0x080015FA` | `0x0809DFC0` | 8 instructions | 46 |
| D | `0x0800160A` | `0x0809DFC0` | 8 instructions | 57 |
| E | `0x0800161A` | `0x0809DFBC` | 7 instructions | 43 |

The Stage E run observed the live SWI/BX LR return at Thumb `0x0800161A` after
Stage D. Stage E compared one native block with zero fallback; state and cycles
matched. Its `STR R5,[SP,#12]` resolved to IWRAM `0x03007DC8`, value zero. The
native final-state delta showed no changed bytes because that word was already
zero, so the run does not independently prove the native store bus effect. The
full log is `/tmp/sma2-stage-e-0800161a-20261016.log`.

The current dynarec build replaces manual Stage F progression with a bounded
four-block boundary-comparison prototype. After A–E pass, it lets the immediate
CPUFastSet SWI/runtime path run as an explicitly unmeasured transition, then
captures the first committed Thumb ROM boundary without assuming its return PC.
It compares at most four consecutive eligible native blocks, one block per
outer step. Each requires exactly one native block, zero fallback, one
unambiguous symbolic exit reason, a non-event branch exit, independent bounded
interpreter replay, and no scheduler event crossing. It stops on the first
mismatch or ineligible boundary. The transition is capped at 16 outer steps / 8
native runs; there is no Stage F entry, gameplay-wide scan, or automatic retry.
This reports the first observed **block-boundary** mismatch only. It cannot
identify an instruction-level divergence or rule out transient differences
that reconverge before a boundary. SWI effects remain outside comparison.

Local ELF hashes:

- Interpreter: `8485ac273cf12ee1f154c55fde4cfdfd6d7d87bcb18d2f87928032c90636307e`
- Four-block dynarec prototype: `5f23c9dba2b4596969ab458e9a8ed27fda98ac3da7dbf0be5d895100d3b027c9`

Both configurations linked. The interpreter ELF has no JIT symbols, the
adapter emitted no warnings, Python overlay compilation and whitespace checks
pass, and focused static assertions cover the four-block cap and eligibility
stops. The prototype has not been run on hardware. Overall gameplay remains
**NO-GO**.

The next hardware action requires separate authorization. Then verify the hash,
confirm the shared loader lock is free and the console is at dcload, and capture
one complete run through the shared wrapper:

```sh
sha256sum /home/gpf/code/SuperMarioAdvance2Recomp/build-dreamcast-jit/SuperMarioWorldRecomp.elf
source /opt/toolchains/dc/kos/environ.sh
set -o pipefail
MGBA_DC_LOCK_TASK=sma2-firstdiv-four-block-20261016 \
  /home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh \
  -t 192.168.0.128 \
  -x /home/gpf/code/SuperMarioAdvance2Recomp/build-dreamcast-jit/SuperMarioWorldRecomp.elf \
  -m /tmp/sma2-dynarec-hw-20261016 \
  2>&1 | tee /tmp/sma2-firstdiv-four-block-20261016.log
```

Expected hash: `5f23c9dba2b4596969ab458e9a8ed27fda98ac3da7dbf0be5d895100d3b027c9`.
The run can validate the four boundary comparisons or stop at the first
ineligible transition. It will not validate SWI/BIOS/device behavior, prove
instruction-level parity, or establish gameplay correctness. Do not launch it
until separately authorized.

## Stage C hardware result — PASS; gameplay remains NO-GO

Stage C is implemented in `src/sh4jit/sma2_sh4_backend.cpp` for a single
candidate block at committed Thumb PC `0x080015FA`. The verified ROM was read
from `/home/gpf/code/dreamcast/gpsp-sh4-jit/ports/dreamcast/roms/Super Mario Advance 2 - Super Mario World (USA).gba` (SHA-256
`63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`). The
current interpreter ELF SHA-256 is
`8485ac273cf12ee1f154c55fde4cfdfd6d7d87bcb18d2f87928032c90636307e`; the
Stage C dynarec ELF SHA-256 is
`7e8abea49debe4a0144706a69dbf6ed5c27dc6be2afb3cff68dea12ee51a2e68`. Both
configurations link; interpreter symbols contain no JIT references, Python
overlay compilation and `git diff --check` pass, and the adapter has no
compiler warnings. The authorized run `sma2-stage-c-080015fa-20261016` passed
Stages A, B and C in one execution. Stage C reached Thumb `0x080015FA` after
the live trampoline with SP `0x03007DBC`, LR `0x080015FB`, CPSR `0x3F`, and
guest cycles `294364`. Its `STRH` at `0x080015FE` wrote `0x00F8` to IWRAM
`0x03007DC4`. One native block, zero fallback, `site_branch` exit at
`0x0809DFC0`; interpreter replay matched all compared state, IWRAM and 46
cycles over 8 instructions. The runner stopped cleanly and returned 0. The
trampoline itself is outside the comparison. This bounded result does not
establish sustained dynarec gameplay support. Do not launch further hardware
runs without separate authorization. Full run details are in
`docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md` under “Physical Stage C result”.

## Current SH-4 dynarec investigation

The interpreter milestone below is historical and remains the known-good
Dreamcast path. The SH-4 dynarec is still **NO-GO**: run 7 reached guest frame 1,
then stalled, and the run was exited. Do not launch another hardware run until
the next one is explicitly authorized.

The targeted diagnostic hardware run is complete and remains **NO-GO**. At
Thumb PC `0x0800044C`, ROM bytes are `F0 B5` (`PUSH {r4-r7,lr}`). The JIT
entered one compiled block, returned `JIT_EXIT_EVENT` without executing the PUSH,
and the runner later entered at `0x0800044E` (`57 46`, `MOV r7,r10`). Source
analysis explains the event: the preceding ARM-to-Thumb JIT branch calls
`_ARMSetMode`, which sets mGBA's absolute `nextEvent` deadline before
`ThumbWritePC` adds branch-refill cycles. The following SH-4 entry therefore
sees `cycles >= nextEvent` and exits before the PUSH. See the hardware log and
detailed source trace in
`docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md`.

The authorized follow-up run captured the interpreter after-state: PC advanced
to `0x0800044E`, SP decreased by 20 bytes, cycles advanced by 10, and LR was
stored at `SP+16`. R4–R7 were zero, and their target words were already zero, so
the trace cannot independently prove those four writes. The JIT did not execute
the PUSH; the interpreter handled it after the confirmed mode-switch event
boundary. No divergence is demonstrated at this instruction. The game still
stalls later, the dynarec remains **NO-GO**, and host state/memory/cycle
differential remains undone. See the exact trace in the integration report.

Both Dreamcast configurations link after the prior change. A targeted
interpreter-shadow differential has now been added to the dynarec adapter for
the `0x0800044E` → `0x080015D8` interval; it is bounded to 64 instructions and
16 writes, stops at the block exit, and compares full CPU/banked state, IWRAM,
and cycles before scheduler commit. One authorized hardware run returned both
paths to `0x080015D8` with matching CPU/banked state, CPSR, IWRAM, and 34-cycle
deltas. The per-run symbolic exit reason and full serialized entry snapshot
were not logged. The outer runner resumed after the differential; the loader
was interrupted after the result and later fallback output appeared. The
console was back at dcload after that run. A later user-authorized attempt
targeted the next interval, `0x080015D8` → `0x0809DFBC`, but the first launch
used a media root without the BIOS, and the corrected-root run did not arm its
differential because the target PC was reached mid-burst. The harness was
fixed to arm at that live block boundary and stop the outer runner; its rebuilt
ELF hash is `5fc26dc9a945729404e63179cb18d616f17e6873152bf1f80f136136b6f23a36`.
After the console returned to dcload, the verified ELF ran once through the
shared wrapper. The interpreter replay and native block both reached
`0x0809DFBC` after 58 cycles; CPU/banked state, CPSR, and full IWRAM matched.
The native exit was `site_branch`, and the outer runner stopped cleanly at the
comparison. This is one passing block boundary only; sustained dynarec
correctness remains **NO-GO**. Do not launch another hardware run until it is
explicitly authorized. Full evidence is in
`docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md`.

The Stage A/B baseline was a two-stage state machine. Stage A is the validated
`0x080015D8 -> 0x0809DFBC` comparison, unchanged; it must pass before the outer
runner is allowed to resume. Stage B arms only at the committed Thumb boundary
`0x080015EC` (the live SWI/runtime trampoline return), snapshots the actual live
state, runs exactly one `ARMJITRun`, captures the symbolic exit reason, block-run
and fallback counts and the observable native write set, then replays the
production Thumb interpreter from the snapshot to the same boundary by its own
decoded semantics. Finite budgets: 128 runner steps, 32 block runs, 512
diagnostic steps; exhausting one reports `INELIGIBLE` instead of searching for
another block. At that point, the runner gate stopped the game after the second
comparison and printed the final summary. The current three-stage extension is
described at the top of this handoff and in the integration report. Review
found and fixed two bounds issues: the step
budget now counts every outer runner step, including interpreter-routed steps,
and Stage B is eligible only when stats confirm exactly one native block run
with no fallback. Both configurations were rebuilt after those fixes: dynarec
diagnostic ELF `build-dreamcast-jit/SuperMarioWorldRecomp.elf`, SHA-256
`bfc9f955b7900912e5658be80a5cbdc19f700c776d1d531b1992ef0b13255fdb`, and
interpreter ELF `build-dreamcast/SuperMarioWorldRecomp.elf`, SHA-256
`8485ac273cf12ee1f154c55fde4cfdfd6d7d87bcb18d2f87928032c90636307e`. Stage B
was subsequently run on hardware and passed; detailed evidence follows. An
earlier authorized launch was cut by the capture pipeline (`head -70` on the
tee'd stream) and produced no comparison. Capture future sessions without
truncating the pipeline.

Hardware result (2026-10-16 run 2, ELF
`bfc9f955b7900912e5658be80a5cbdc19f700c776d1d531b1992ef0b13255fdb`): stage 1
**PASS** and stage 2 **PASS**. Stage 2 was armed at the trampoline return,
executed exactly one native block from `080015EC` (`block_runs=1 fallback=0`,
43 cycles), exited at `0809DFBC` with reason `site_branch`, and the replay reached
the same boundary by its own decoded semantics (`arrived_via=branch`, 7
instructions, 43 cycles, 1 recorded write). CPU, CPSR, IWRAM and cycles all
matched; the outer runner stopped cleanly and the program returned 0.

The dynarec is still **NO-GO** for gameplay: three bounded intervals are
validated, not the interpreter as a whole. A comparison that starts at `0809DFBC` and
crosses its Thumb SWI #0x0C thunk is **INELIGIBLE** with the current shadow
replay. A source-derived Stage C candidate is `080015FA -> expected 0809DFC0`.
The BL suffix at `080015F8` sets LR to `080015FB`, so `BX LR` would resume at
`080015FA`; hardware has not observed this return for the Stage C caller. The
BL pair at `08001606/08001608` targets `0809DFC0` using the full suffix imm11
`0x4DB`, not `0x0DB`. The initial `STRH` at `080015FE` writes to SP+8; whether
that is shadow-supported must be measured from the live state. Before
At the time of this historical note, Stage C had not yet run. It subsequently
passed as recorded in the current handoff above and the integration report.
Do not infer that the preceding BIOS handler is validated by the post-SWI
Stage C comparison.

The interpreter-first SMA2 build now boots the local retail ROM on a physical
Dreamcast and presents actual GBA PPU output. The user directly observed the
“Nintendo Presents” screen followed by the single-player/multiplayer title
menu. The user reports that it is advancing very slowly. Do not return to
feasibility research; continue hardware debugging and input/audio integration.

## Current milestone status

- Interpreter starts on Dreamcast: **PASS** (console log).
- SMA2 ROM and BIOS load: **PASS** (4 MiB ROM and 16 KiB BIOS in console log).
- Actual GBA framebuffer renders: **PASS** (direct physical observation).
- “Nintendo Presents” splash visible: **PASS** (direct physical observation).
- SMA2 title menu visible: **PASS** (direct observation; single/multiplayer choices).
- Second startup splash: **PASS** (user directly reports reaching it).
- Startup speed: **SLOW** (direct qualitative report; no frame rate captured).
- Controller menu input: **PASS** (D-pad up/down navigates; user advanced through menu choices).
- Right trigger as GBA A: **PASS** (hardware trace `rtrig=243`, `KEYINPUT=0x03fe`).
- Gameplay: **NOT CONFIRMED** (Super Mario World title screen reached, no level yet).
- Audio output: **PASS** (user hears game music on KOS PCM; tempo sounds slow with guest execution).
- Sustained gameplay: **NOT TESTED**.

## SH-4 dynarec backend

- Backend selected: **Bruce mGBA ARM/Thumb → SH-4 dynarec** (revision
  `f3eb1bbad6dfd5be2c48ee599d1bbcf7ba33f41b`, MPL-2.0), chosen over gpSP's SH-4
  JIT because it reaches CPU state, memory and events through function-pointer
  hooks, so GBARecomp keeps owning bus/MMIO/PPU/audio/timers/DMA/IRQ.
- Vendored snapshot: `vendor/mgba-sh4-jit` (61 files) produced by
  `tools/vendor_mgba_sh4_jit.sh`; provenance and license in
  `vendor/mgba-sh4-jit/PROVENANCE.md`. Neither source dynarec repository was
  modified.
- Adapter: `src/sh4jit/sma2_sh4_backend.cpp`. **Compiles for SH-4 and now links
  into the SMA2 build** (`SMA2_SH4_DYNAREC=ON` →
  `build-dreamcast-jit/SuperMarioWorldRecomp.elf`), and `step_once` is wired to
  route to it. Gate A PASS.
- Backend selection is runtime-selectable; the interpreter remains the default and
  the fallback. On the Dreamcast the choice is read from
  `/pc/sma2_cpu_backend.txt` (kos-tool cannot hand `main()` an argv tail); the
  boot banner prints `sma2_cpu_backend=interpreter|sh4-dynarec`. No fallback is
  hidden: BIOS and mutable guest RAM are refused by
  `sma2_dc_jit_region_allowed()` and fall through to the production interpreter.
- Measured dynarec allocation cost: ≈ 1,321,784 B heap (1.26 MiB) plus ≈ 9 KB
  statics. See `docs/DREAMCAST_MEMORY_BUDGET.md`.
- Correctness parity vs. the interpreter (registers, banked registers, CPSR/SPSR,
  guest PC, Thumb/ARM state, cycle totals, memory effects, exceptions/IRQ): **NOT
  TESTED**. Cycle parity is the open risk — GBARecomp charges fetch waits per
  instruction with no ARM prefetch state, while mGBA's ARM core maintains
  `prefetch[0]/prefetch[1]` and charges through `GBAMemoryStall`.
- Physical Dreamcast smoke run: **NO-GO**. The user authorized a bring-up run
  before Gate B; JIT initialization succeeded and the first frame was reported,
  then unsupported opcodes and a stuck/black screen followed. The user exited
  cleanly. This is not a correctness or support pass; stop further hardware runs
  until the state/opcode mismatch is explained by differential validation.
- **Gate B (host differential running actual generated SH-4) is BLOCKED.** The
  only host path for generated SH-4 is `jit-host.c` + `sh4-interp.h`, which
  requires a `-m32` binary (jit-host.c:11 asserts `sizeof(void*) == 4`), while
  the differential must also use GBARecomp's production interpreter, which lives
  in the 4,000-line `runtime.cpp` runner. That needs a second, separate `-m32`
  configure of the tree with a new `SMA2_JIT_HOST` CMake branch. See
  `docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md` section 9 for the two routes.

Full detail: [`docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md`](docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md).

## Build and next run

Run `tools/build_dreamcast.sh`. It stages SMA2-only KOS compatibility changes
in ignored `build-dreamcast/gbarecomp-overlay/`; the separate GBARecomp checkout
remains untouched. The build excludes the generated cart corpus and uses the
production interpreter. The ELF is `build-dreamcast/SuperMarioWorldRecomp.elf`
with map `build-dreamcast/SuperMarioWorldRecomp.map`.

The latest ELF is 2,299,881 B by SH-4 `size` (text 1,145,721; data 8,176; BSS
1,145,984). This is executable accounting, not a full peak-RAM guarantee. ROM,
emulated memories, thread stack, KOS and display resources still need a measured
runtime headroom pass.

At the start of `main`, a Maple callback exits on Start+A+B+X+Y. The current
hardware build uses the interpreter, KOS mono PCM, and right-trigger-to-GBA-A
mapping. The user has directly observed both startup splashes, the title menus,
and the Super Mario World title screen; D-pad navigation and audible music
worked. The user reports very slow execution and odd music timing. Gameplay is
not yet confirmed.

The latest diagnostic launch used the shared lock wrapper and a 180-second
timeout; see `/tmp/sma2-video-present-counter-20261015.log`. The user directly
observed the second splash and then rebooted to dcload. The `HostWindow::present()`
counter reported 60 video handoffs, but no guest frame telemetry appeared, so
guest speed still lacks a reliable measurement. The hardware lock is free.

All physical launches must use
`/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh`, with a new
`MGBA_DC_LOCK_TASK`, after verifying no other `kos-tool` session holds the
console. Preserve the existing session if the user is actively playing; only
reload once they finish or can cleanly exit.

## Dynarec next actions

1. Explain the first observed downstream divergence. The bounded comparison
   found no divergence at `0x080015D8`; it does not establish parity after that
   block or inside later blocks.
2. The next diagnostic should capture a per-run symbolic exit reason and full
   entry snapshot if that evidence is needed. Get explicit authorization before
   another hardware run.
3. Do not classify later unsupported opcode values until their fetch address
   and ARM/Thumb width are verified.

## Repository boundaries

- Do not push without separate authorization.
- Do not initialize, update, repin, or modify the GBARecomp checkout.
- Keep SMA2-specific platform integration and compatibility overlay here.
- Do not claim controller, audio, or sustained-play success without direct
  evidence.
- Detailed current observations and limitations are in
  [`docs/SMA2_DREAMCAST_PORT_STATUS.md`](docs/SMA2_DREAMCAST_PORT_STATUS.md).

## Snapshot integration and memory gate — 2026-10-16

Snapshot host regression and AOT integration remain passing. Memory preflight is **PASS** for hardware validation. Independent KOS tracing attributed the two historical 3 MiB failures to the optional AOT IRQ diagnostic vector log; normal SMA2 Dreamcast builds also remove a separate optional 3.67 MiB audio capture history through an SMA2-only staged framework overlay. The checkpoint-enabled memory-only probe measured 9,453,568 bytes (9.016 MiB) of contiguous `sbrk` top-growth interval after runtime initialization. Source/host estimates put snapshot save and restore peaks near 2.3 MiB and 1.6 MiB respectively. Details: [`docs/results/SMA2_SNAPSHOT_MEMORY_PREFLIGHT_20261016.md`](docs/results/SMA2_SNAPSHOT_MEMORY_PREFLIGHT_20261016.md).

### Snapshot hardware validation — 2026-10-16

H1 missing-file startup **PASS**. H2 synthetic input exercised two normal Y rising edges and safe-boundary saves; physical Y input is **NOT RUN**. H3 checkpoint structure/identity/CRC **PASS**. H4 startup autoload and bounded interpreter execution **PARTIAL/PASS for load and bounded resume**, before deterministic comparison. H5 is now **PASS** for 64, 1,024, and 16,384 retired interpreter instructions on physical hardware: immediate restored state and every serialized endpoint section matched the uninterrupted reference byte-for-byte. The 16,384 run advanced frame/PPU state and audio generation; no IRQ/timer/DMA event coverage was demonstrated. H6 invalid-file rejection remains **NOT RUN**. Details, all six raw logs, ELF hashes, checkpoint images, and section comparisons: [`docs/results/SMA2_SNAPSHOT_H5_DIFFERENTIAL_20261016.md`](docs/results/SMA2_SNAPSHOT_H5_DIFFERENTIAL_20261016.md) and `docs/results/evidence/sma2_snapshot_h5_20261016/`.

Final snapshot gates — 2026-10-16: H6 **PASS** for wrong ROM, wrong BIOS, unsupported format version, ABI mismatch, truncation, CRC corruption, oversized section length, malformed section directory, and file-as-directory I/O failure. Each produced a specific failure and terminated without checkpoint acceptance or guest continuation; all nine application exits and launcher/lock cleanup were recorded. The preserved H5 checkpoint remained hash-identical. Post-rejection recovery **PASS** with immediate exact restore and a 64-instruction endpoint matching the H5 reference. Host regression **45/45 PASS**. Physical Y input remains **NOT RUN** because this tool session cannot actuate/observe the controller. IRQ, timer-service, and DMA-service event differentials remain **NOT DEMONSTRATED**; the 16,384 H5 trace observed frame/audio progression but no IRQ/timer/DMA service. Full evidence: [`docs/results/SMA2_SNAPSHOT_FINAL_HARDWARE_GATES_20261016.md`](docs/results/SMA2_SNAPSHOT_FINAL_HARDWARE_GATES_20261016.md) and `docs/results/evidence/sma2_snapshot_final_gates_20261016/`.

Remaining snapshot follow-up: perform the documented manual physical Y hold/release/repress and exit-chord test. If event coverage is needed, build a separate bounded production-scheduler fixture that reaches actual timer/IRQ or DMA service in both uninterrupted and restored paths; do not inject events into ordinary gameplay or claim coverage without matching event traces. Keep interpreter forced and production AOT disabled.

## Post-IRQ checkpoint AOT eligibility — 2026-10-16

Focused reachability evidence is recorded in [`docs/results/SMA2_POST_IRQ_AOT_ELIGIBILITY_20261016.md`](docs/results/SMA2_POST_IRQ_AOT_ELIGIBILITY_20261016.md). `gf_tfunc_0809E510` was not reached in the restored continuation. `gf_tfunc_0809B4C8` was reached once in interpreter CONTROL and is a locally simple generated Thumb block, but neither AOT launch demonstrated native entry. Static-recompiled mode followed a different interpreter-bridge trace before the candidate and ended with a 56-cycle scheduler/device mismatch; the one-function AOT differential is NO-GO.

Do not expand the generated table or repeat the hardware run yet. First explain the CONTROL/AOT-mode first-trace divergence. Then review a candidate that the actual post-restore dispatch path reaches, or audit the exact checkpoint resume PC `0x0809B276` before considering it. Keep the production `GBARECOMP_FORCE_INTERP=1` policy and all AOT work uncommitted.

## Two-function checkpoint AOT differential — 2026-10-16

The 16,384-instruction CONTROL trace ranked `gf_tfunc_0809D77C` as the most
useful distinct reached candidate after excluding the `0x08000532` frame-wait
loop. It was entered three times in the 256-instruction two-function hardware
test. However, the two-function AOT endpoint failed comparison at 256:
CONTROL ended at `0x0800150A`, `r2=0x03002D70`, 56,425,211 cycles; AOT ended at
`0x08001508`, `r2=0x03002D68`, 56,425,209 cycles. Native diagnostic count was
41, but is not accepted coverage evidence while endpoints differ. Do not run
1,024/4,096/16,384 for this configuration until a first-divergence trace
explains and fixes the mismatch. The passing one-function result is preserved.
Details and both failed raw logs: [`docs/results/SMA2_AOT_TWO_FUNCTION_DIFFERENTIAL_20261016.md`](docs/results/SMA2_AOT_TWO_FUNCTION_DIFFERENTIAL_20261016.md).

The prior full host executable link failure remains a separate build
configuration issue; it is not resolved or conflated with this Dreamcast
endpoint failure.

## AOT compression research — 2026-10-17 — HOLD

Offline research completed in `docs/results/aot_compression/00_ARCHITECTURE_BASELINE.md` through
`08_FINAL_RECOMMENDATION.md`. The available 342-function compiled Reset sample compresses to 78.8%
with independent LZ4 under the generic 64 KiB distance, but only 94.7% under the local Dreamcast
build script's `LZ4_DISTANCE_MAX=32`. Shared dictionaries lose after residency cost on held-out data
and are net losses under the 32-byte configuration. Two raw generated functions have only
`R_SH_DIR32` code relocations and no direct cross-reference, but no relocation/execution loader has
been proved. Full cache hit rates and RAM headroom remain unknown; the full 64-shard compiled object
set was absent from the current `/tmp` paths.

Next recommended experiment: build-only B16-K loader/dispatch infrastructure with no module present,
gate hot symbol bytes/data placement against B16/L0B16, then (only if static gates pass) run the
original locked diagnostic window and unchanged timing acceptance criteria. No compression or
dictionary should be included in that control. No hardware was launched and production AOT execution
was not modified.

## LZ4 / SH-4 decoder study — 2026-10-17 — HOLD

Read-only source audit and bounded host-oracle sketch are under `docs/results/lz4_sh4/00_IMPLEMENTATION_AUDIT.md`
through `08_FINAL_RECOMMENDATION.md`. The local LZ4 `LZ4_DISTANCE_MAX=32` build define constrains compressor history/search, while raw LZ4 decoder offsets remain 16-bit format offsets. SH4ZAM `memcpy` uses FPU `fmov.d` bulk transfers and explicitly requires non-overlapping ranges; safe match-copy dispatch is only when `match_length <= distance`, with forward overlap copy otherwise. DCSinge's source calls LZ4 APIs, but active linkage to the SH4ZAM macro-mapped archive is unproven.

The current harness passes deterministic roundtrip/canary and a small malformed-input subset, including ASan/UBSan with leak detection disabled. LeakSanitizer itself cannot run in this ptrace environment. Exact per-distance blocks, dictionary boundaries, broad malformed corpus, separate decoder candidates, and target benchmark are incomplete. Per the correctness gate, no Dreamcast launch was made. Next experiment: finish the exact-offset/dictionary/malformed host oracle and candidate matrix; only after full PASS, build and launch one isolated decoder fixture through the locked launcher. No production decoder, SMA2 runtime, DCSinge, KOS-ports, hardware config, or git history was changed.

## LZ4 SH-4 correctness closure / hardware benchmark — 2026-10-17 — HOLD

Host H1 now passes against independent system liblz4 1.9.4: 469 valid checks, 11 malformed rejection cases, 20,000 bounded seeded fuzz cases, exact offsets through 65,535, overlap/dictionary cases, and four preserved AOT fixtures; ASan/UBSan passed (LSan unavailable under ptrace). Final DCSinge ELF and KOS archive prove the active LZ4 symbols and SH4ZAM linkage; the safe decoder's offset-aware match path uses non-overlapping SHZ copy chunks. The standalone generic/SHZ-safe/DCSinge-fast/dictionary fixture ELFs and maps are preserved under `docs/results/evidence/lz4_sh4_correctness_hardware_20261017/`.

The one authorized D1 launch attempt acquired/released the lock, then failed before `kos-tool` execution because KOS `environ.sh` was not sourced (`kos-tool: command not found`). No application reached the console; no performance launch followed, and no retry/reset occurred. `kos-tool` resolves after sourcing the environment (offline preflight). Next: in a separate hardware session, run the saved correctness ELF with that environment sourced; only after D1 passes, run the performance ELF. Full report set: `docs/results/lz4_sh4/09_HOST_CORRECTNESS_CLOSURE.md` through `14_FINAL_ENGINEERING_VERDICT.md`.

## LZ4 / SH-4 hardware follow-up — network stop — 2026-10-17

After user confirmation of console availability, a fresh D1 launch used the preserved, hash-verified check ELF. The KOS environment resolved correctly in the wrapper shell. One lock-wrapper invocation reached `kos-tool`, which exited 1 before ELF loading with `Failed to create UDP sockets` / `Failed to initialize network transport`; the wrapper lock was released normally. No application result or performance data exists; no retry or reset occurred. The earlier `nounset`-affected preflight exited before wrapper invocation and is separately recorded. HOLD remains. Resolve the loader/network transport issue, then wait for explicit authorization before a fresh D1 session. Evidence: `docs/results/lz4_sh4/16_HARDWARE_SESSION_20261017_NETWORK_STOP.md` and `docs/results/evidence/lz4_sh4_hw_session_20261017_d1_fresh/`.

## LZ4 / SH-4 authorized network recheck — 2026-10-17 — HOLD

After the user's renewed authorization, a fresh D1 session used the hash-verified preserved ELF and KOS environment. `kos-tool` again failed before ELF execution with `Failed to create UDP sockets` / `Failed to initialize network transport` (exit 1). The wrapper released the lock normally; no application ran. No retry or performance launch followed. D1 remains unmeasured. Resolve loader/network transport before another explicitly authorized session. Report and raw evidence: `docs/results/lz4_sh4/17_HARDWARE_SESSION_20261017_NETWORK_RECHECK.md`, `docs/results/evidence/lz4_sh4_hw_session_20261017_d1_network_retry_authorized/`.

## LZ4 / SH-4 D1 and hardware performance — 2026-10-17 — CONDITIONAL GO (offline decoder feasibility)

A fresh locked session passed D1 on physical Dreamcast: all 28 stream/decoder cases passed exact output hashes and guard checks across small, medium, large, grouped, distance-32, full-window, and dictionary fixtures; launcher exit 0 and `Program returned 0`. The preregistered performance ELF repeated correctness and completed 56 standard timing rows plus eight dictionary rows; result PASS, launcher exit 0, `Program returned 0`. Installed SHZ-safe beat generic-safe on all identical-stream cases (15.7–39.1% warm median improvement). DCSinge fast won small and grouped distance-32 cases and all full-window cases; installed SHZ-safe won medium/large distance-32. Full-window payload decreased 29.0% across these fixtures but DCSinge-fast warm median decode time rose 6.5–76.3%. The four-fixture dictionary package loses once its 65,535 B resident dictionary is counted, and dictionary decode was slower than corresponding distance-32 SHZ-safe decode. Runtime SHZ call counts were not instrumented. Decoder feasibility is established for fixtures; production use remains gated by AOT relocation/cache work. Detailed table, limitations, hashes, and logs: `docs/results/lz4_sh4/18_DREAMCAST_D1_AND_PERFORMANCE_20261017.md` and `docs/results/evidence/lz4_sh4_hw_session_20261017_d1_network_enabled/`. No production integration or rebuild.
