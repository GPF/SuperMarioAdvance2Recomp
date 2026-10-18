# SMA2 Gate 7B — B16/L0B16 AOT-bridge trace feasibility — 2026-10-17
Read-only research. Nothing was built, linked, launched or executed (no emulator, no hardware); no source edits, no commits; Gate 5/6/7A evidence untouched.
Evidence: `/tmp/nulldc3-sh4trace-gate7b-20261017/` (`artifact_inventory_sha256.txt`, `boundaries.txt`, `*_nm_sizes.txt`). Labels: [V] verified from artifact/source/log, [D] derived, [E] estimate, [U] unknown.
Source line numbers refer to the overlay actually compiled into the hardware ELFs (`/tmp/sma2-lowpmu-B16/gbarecomp-overlay/src`, hash-matched below) and to `/home/gpf/code/SuperMarioAdvance2Recomp/src`.

## Verdict: **CONDITIONAL**
The frozen B16/L0B16 hardware ELFs (Level A) can in principle be traced unchanged, and their measured window has exact, breakpoint-able boundaries in both binaries. But they cannot be traced by media alone: they hard-code `/pc` (dcload) for every input, and nullDC3 has no `/pc`. A separately approved step is required (a nullDC3-side `/pc` provider is the smallest change that leaves the SMA2 binaries untouched). The whole 16,384-instruction window is ≈ 28 M SH-4 instructions, above the recorder's 10 M cap, so a bounded-sampling plan is specified.

## 1. Artifact inventory and provenance
| Role | Path (under `docs/results/evidence/`) | SHA-256 | Status | Provenance |
|---|---|---|---|---|
| **B16 hardware ELF (selected)** | `sma2_low_perturbation_pmu_20261017/elf/B16-LOWPMU-sel.elf` | `60c1289c02082c809deabf901dbe4831ca04527062a3685248989617e850dc8f` | frozen (`SHA256SUMS`, driver `sha256sum -c` before every launch) | = base ELF + 1 literal-pool word (`B16-patch.log`); hardware-run for the +68 ms and PMU results |
| **L0B16 hardware ELF (selected)** | same dir `L0B16-LOWPMU-sel.elf` | `f40fc020dcde51c9b882831f6289d36cee746fb7419c14b44d8f3ca1eb4eec57` | frozen | same |
| B16 base (wrapper linked, uncalled) | `…/B16-LOWPMU-base.elf` + `.map` | `f0e71d64…e13e` / map `b99bc3c7…` | frozen; **independently regenerable**: identical to `/tmp/sma2-lowpmu-B16/SuperMarioWorldRecomp.elf` (hash match) | build dir + overlay + CMakeCache still on disk |
| L0B16 base | `…/L0B16-LOWPMU-base.elf` + `.map` | `b13f3b7b…57e5` / map `aafa6b29…` | frozen; equals `/tmp/sma2-lowpmu-L0B16/…elf` | same |
| B16 reference (uninstrumented timing baseline) | `sma2_aot_bridge_batching_20261017/timing/Q-B16.elf` | `5da59d0648f4faca1f53cfd8c0624f16b155451e42c3ff8e7e39a64db3c98abd` | frozen | source of the 366.469 ms baseline |
| L0B16 reference | `…/timing/Q-L0B16.elf` | `31237bd2f1a26de3b92cd7358e2633672b8a229292e83844e48ba7f7c0fde7a7` | frozen | 440.908 ms baseline |
| Experimental | `Q-A2B16`, `Q-DMB16`, `TAIL-*`, `B16-0x24…` (old PMU build, failed gate P0), `v1/v2/v3-*` rejected PMU designs | various | not used | — |
| **Checkpoint** | `sma2_aot_from_checkpoint_20261016/postirq_safe_checkpoint.gbas` (= `sma2_checkpoint.bin` in every run media dir, 151 copies) | `50282ea4be8bea13b9063a38438dcde0b0b330bf0f3b929edf525bb112af32e6` (635,929 B) | frozen | captured by the H5 diagnostic (`SMA2_POST_IRQ_SAFE_CHECKPOINT_20261016.md`) |
| Media source | `/tmp/sma2-parity-C-16384-media-20261016/` | ROM `63d9fff0…a4f3`, BIOS `fd254772…6570`, `game.toml` `ecbb5dd7…f2a`, save `d7f59620…38e73` | /tmp, hash-matches the gold manifest | gold: `sma2_aot_tail_hardware_20261017/timing/b01-B16/Q-B16-inputs.sha256` |
| Build recipe | `build_lowpmu.sh`, `build_variant.sh`, `build_q.sh`, `/tmp/sma2-lowpmu-*/CMakeCache.txt` | (cache `eadeecae…`, `c065a3d2…`) | frozen / on disk | B16: `SMA2_DEV_CHECKPOINT=ON _AUTOLOAD=ON`, `SNAPSHOT_H5_DIAG=ON`, `AOT_MODE_PARITY_DIAG=ON`, `H5_INSTRUCTIONS=16384`, `-DSMA2_SNAPSHOT_H5_TIMING=1`, `SMA2_LOW_PMU=ON`, bridge batch 16 + deferral + fast-miss; B16 generated dir = empty, **L0B16 = `gen-dummy-len0` (`kDispatchTableLen = 0`, two generated functions linked)** |
| Linker script | KOS default `/opt/toolchains/dc/kos/utils/ldscripts/shlelf.xc` (no custom script for these builds) | — | system | G5 ordering-file experiments are separate |
| Hardware logs | `sma2_low_perturbation_pmu_20261017/runs/*/…-hardware.log`, `sma2_lowpmu_followup_events_20261017/runs` | per-run `SHA256SUMS`/`inputs.sha256` | frozen | 24 + 30 launches |
Identity is established by hashes recorded in the driver and matched here, not by filenames. **Caveat:** `Q-*` originals have no preserved build dir; the `*-sel` ELFs are patched copies of the preserved `*-base` ELFs.
**Figures re-verified against the source reports:** B16 366.469 ± 5.011 ms, L0B16 440.908 ± 5.797 ms, +74.439 ms (`SMA2_AOT_TAIL_HARDWARE_20261017.md:7,45`); **I-cache misses (event 0x08) 997,657 vs 1,343,802, +346,146 (+34.7 %)** and **instructions issued (0x13) 27.90 M vs 28.43 M** (`SMA2_LOWPMU_FOLLOWUP_EVENTS_20261017.md:14-16`); **I-cache freeze cycles (0x24) 24.80 M vs 36.50 M, +11.696 M ± 0.005 M** (`SMA2_LOW_PERTURBATION_PMU_HARDWARE_20261017.md` §4). The 11.696 M is a *freeze-cycle* difference from a different experiment, not an extra-miss count.

## 2. Execution path of the measured window (B16 and L0B16)
`_arch_main`→`run_sma2`→`run_game` (`main.cpp`, old frozen version) → **`HostWindow::open` → `startup_autoload`** (`dreamcast_host_window.inc:240-285,305`): `autoload_checkpoint("/pc/sma2_checkpoint.bin")`, then `h5_diag::compare_immediate_restore` which re-reads the file and **writes `/pc/sma2_h5_immediate.bin`** (`sma2_snapshot_h5_diag.cpp:39,402-425`) and only then sets `armed = true`. If the checkpoint is NOT_FOUND the program prints `CHECKPOINT_NOT_FOUND … continuing normal interpreter startup` and **never arms**: no window, cold boot instead [V source].
Main loop (`runtime.cpp:2317`): `step_once` → `runtime_dispatch` → `dispatch_resolve` (`runtime_arm.cpp:863`; `lookup_in` miss; `sma2_aot_dispatch_try` is not compiled in) → `runtime_dispatch_miss` (`runtime_arm_default_aborts.cpp:1001-1060`) → `runtime_bridge_interpret(…, max_instrs=1)` (`:489`), which with `SMA2_AOT_MODE_PARITY_DIAG` batches up to **16 guest instructions per entry** (`sma2_batch_ok`, `:555-558`; batch loop `:734-760`), each doing `ThumbDecoder::decode`, `Interpreter::step`, `runtime_tick`, and **`sma2_snapshot_h5_instruction_retired` (`:710`) → `retire_impl`**. Hardware logs: `cpu_backend=static-recompiled`, `dispatch_misses=1031`, `interpreted_insns=192`, `native=0`, `SNAPSHOT_H5_EXECUTION_COUNTS total=16384 interpreter=16384 native=0` in both builds [V].
**Measured window:** `retire_impl` index 0 takes the start timestamp (call of `sma2pmu_get_ticks`, whose mode-file `open/read` of `/pc/sma2_pmu.txt` happens *before* the real timestamp; absent file ⇒ inactive pass-through, the layout-only control); at `retired == 16384` it takes the end timestamp, prints `SNAPSHOT_H5_TIMING`, then waits for a safe boundary and writes `/pc/sma2_h5_endpoint.bin` (outside the window) (`sma2_snapshot_h5_diag.cpp:494-560`; `SMA2_LOW_PERTURBATION_PMU_BUILD_20261017.md` §2).
**L0B16 executes zero native functions** [V]: `kDispatchTableLen = 0` (`gen-dummy-len0/dispatch_table.cpp:15`), `native=0` in every log; the two generated functions `gf_tfunc_080740EC/0809B4C8` (L0B16 `0x8C011680`, `0x8C013670`) are linked but unreachable — they only move the hot code (+13.7 KB `.text`).
**Why the Gate 7A path is not equivalent [V]:** Gate 7A = `GBARECOMP_FORCE_INTERP=1` → `step_once` → `runtime_force_interp_step` (one guest instruction per call, no `dispatch_resolve`/`runtime_bridge_interpret`/`retire_impl`/batching), different hot functions and addresses, no checkpoint, early-boot frame 10 (one 81 k-instruction SWI). Different code, different layout (`.text` 1,154,493 B vs 1,019,044 B B16), different workload.

## 3. Checkpoint packaging
* The frozen ELFs open **only `/pc/…`**: `game.toml`, `gbarecomp/bios/gba_bios.bin`, `roms/super_mario_world_usa.gba`, `sma2_checkpoint.bin`, `sma2_pmu.txt` (read) and **write** `sma2_h5_immediate.bin`, `sma2_h5_endpoint.bin`, `/pc/sma2_exit.txt` (poll) — `strings` of both ELFs: no `/cd/` string, no command-line/env override for the checkpoint path (`kCheckpointPath` constant, `dreamcast_host_window.inc:48`). The `/pc`→`/cd` fallback of the Gate 4 ELF is in a different source tree and does not apply [V].
* **Media-only is not possible** for Level A: a CDI can hold the files but the ELF never looks on `/cd`. And `/pc` exists only if KOS detects dcload: `*(u32*)0x8C004004 == 0xdeadbeef` and a syscall function pointer at `0x8C004008` (`fs_dcload.c:421-460`, `dcload_syscalls.c:19-35`; 22 syscalls: open/read/write/lseek/close/creat/stat/opendir…). On hardware this is dcload-ip, mapped to the media dir with `kos-tool -m` (`run_hw_lowpmu.sh`).
* Options: **A2 (preferred): nullDC3-side dcload syscall HLE** — emulator-only change (Gate 4 worktree): write the magic and a trapped vector at start, implement the syscalls on a host directory (mapping = `-m media`, read/write needed). SMA2 bytes/hashes untouched. **A1:** real dcload under nullDC3 via its PTY (`Debug.VirtualSerialPort`, `libswirl/linux/common.cpp:392`) + `kos-tool -T dc_serial`; **no dcload image or dc-tool is on this machine** [V search] and SCIF timing at dcload's baud is untested [U]. **B (smallest SMA2 change):** autoload/asset/H5-file paths with `/cd` fallback and non-fatal H5 writes → a **new ELF** (see §6).
* Boot container for Level A: the Gate 5 route (real BIOS + CDI with `1ST_READ.BIN`) needs a new CDI (media creation, not authorised); the ELF's PT_LOAD content can be shown identical to the disc payload exactly as Gate 6 did. `nulldc3 <elf>` (REIOS) is a second route; REIOS booting this particular KOS ELF is untested [U].
* ROM/BIOS/`game.toml`/save must be the gold hashes; the checkpoint validates ROM/BIOS SHA-1 itself (`startup_autoload`: `kSma2RomSha1`, `kSma2BiosSha1`). No emulator state is needed beyond a working dcload and the same KOS environment [D].

## 4. nullDC3 compatibility (frozen ELFs) — not assumed from Gate 7A
| Item | Assessment |
|---|---|
| Instruction support | same toolchain/flags as the Gate 7A ELF, executed by the same interpreter; B16-specific code (bridge, retire hook) is ordinary integer code [D]; full coverage unverified [U] |
| `/pc` | **blocker without a provider** (§3) |
| PMU | KOS/our wrapper touch PMCR at `0xFF000084/88`; nullDC3's CCN range ends at `0x1F00003C` (`sh4_mmr.cpp:575-584`) → writes are dropped with an error log (already seen in Gate 5 at `0x8C07323C`). **Inactive mode (no `sma2_pmu.txt`) is unaffected; active PMU runs are meaningless** under nullDC3 |
| Cache/timing | nullDC3 does not model the SH-4 I-cache or its stalls; `window_us` will be emulated time. Trace ≠ hardware misses |
| Timers/IRQs | TMU/KOS tick present (Gate 5/7A: 0.4 % KOS instructions in a window); ≈ 400 ticks in a 28 M-instruction window [E] |
| Peripherals | video/AICA/maple/audio-none ran in Gate 5/7A; H5 file I/O and `recomp_*.toml.frag` writes are non-fatal when failing (hardware logs show "could not open") except the H5 immediate/endpoint writes, which must succeed (`startup_autoload` fails otherwise) |
| Debugger/recorder | Gate 4 mechanisms apply; breakpoints stop before the instruction; arming at a stop; recording is interpreter-only [V] |
| MMU/cache assumptions | KOS runs without MMU; nullDC3 builds `NO_MMU`-style address rule (`dbg_sh4.h:MaskAddr`) [V] |
Conclusion: the frozen binaries have **no hardware feature that blocks execution** apart from `/pc`; PMU activity would be invalid and cache behaviour is not reproduced.

## 5. Capture boundary (resolved separately per ELF; bytes verified in each ELF)
| | B16 (`60c1289c…`) | L0B16 (`f40fc020…`) |
|---|---|---|
| `retire_impl` entry (per guest instruction; first hit = window index 0, before t0) | `0x8C01BEB0` bytes `862f962f` | `0x8C01FE58` bytes `862f962f` |
| **window start RA** (`mov #80,r7` after `jsr @r0; nop`, first instruction after the start timestamp, hit once; not a delay slot) | **`0x8C01BEFA`** (`50e7`) | **`0x8C01FEA2`** (`50e7`) |
| **window end RA** (after the end timestamp, hit once) | `0x8C01BFE4` | `0x8C01FF8C` |
| `sma2pmu_get_ticks` (called at both edges; first call reads the mode file) | `0x8C108DA0` | `0x8C10C300` |
| `capture_named` (endpoint/immediate writer) | `0x8C01BCF8` | `0x8C01FD30` |
| `runtime_bridge_interpret` / `dispatch_resolve` / `runtime_dispatch` | `0x8C052874` / `0x8C080654` / `0x8C081894` | `0x8C055120` / `0x8C0839E0` / `0x8C084854` |
| `ThumbDecoder::decode` / `Interpreter::step .part.0` | `0x8C077148` / `0x8C077CA0` | `0x8C07A084` / `0x8C07ABDC` |
| `runtime_tick` | `0x8C040938` (+ `.part.0` `0x8C040060`) | `0x8C04171C` (single) |
| `startup_autoload` / `HostWindow::open` | `0x8C04E82C` / `0x8C05384C` | `0x8C051010` / `0x8C0560DC` |
| H5 flags `armed` / `complete` / `retired` | `0x8C1A56A1` / `0x8C1A56A0` / `0x8C1A86AC` | `0x8C1A988D` / `0x8C1A988C` / `0x8C1AC898` |
| `g_cpu` (R15 at +0x3C) / `g_runtime_vblank_starts` | `0x8C1AC914` / `0x8C141DF0` | `0x8C145EAC` / `0x8C145FD8` |
Addresses differ between the binaries (layout), which is exactly why they are resolved per ELF. Frequency: start/end RAs once per run; `retire_impl` 16,384 times.
**Preferred boundary:** breakpoint at the *window start RA* in the ELF under test; at the stop verify `armed==1`, `retired==0`, guest R15 `0x0809B276`, CPSR `0x6000003F`, cycles `56,424,198`, VBlank 201, IRQ entries 192 (values from `B16-0x24-hardware.log`/`SMA2_POST_IRQ_SAFE_CHECKPOINT_20261016.md`); arm; remove the breakpoint; resume. The trace then starts at the first measured instruction (after t0). Existing RPCs suffice for stop/verify/arm/resume. Emulated time and guest state are unchanged by stops (R7 of the breakpoint spec); recorder activation does not alter execution (Gate 4 selftest T4).

## 6. Bounded trace length (Critical issue)
Measured window size [M, hardware PMU 0x13]: **27.90 M (B16) / 28.43 M (L0B16) SH-4 instructions issued** per 16,384 guest instructions ⇒ **≈ 1,703 / 1,735 SH-4 instructions per guest instruction** (cf. Gate 7A ordinary steps ≈ 2,000 [V]; not transferable between paths). Window composition [V]: 14,075 idle-poll guest instructions (85.9 %, 3,518 iterations of the 4-instruction loop `0x0800052E-0x08000534`), 2,309 non-idle, front-loaded in the first ~2,000.
Full capture = 28 M records × 16 B ≈ **446 MB** > recorder maximum (10,000,000 records, 160 MB). The recorder cannot be chained into consecutive segments because it has no "pause at limit" (segment boundaries cannot be hit exactly) — **a complete-window trace needs a recorder change (higher cap or pause-on-limit) that is not implemented**.
**Bounded sampling plan (implementable now, no recorder change):**
* **S1 head:** arm at the start RA, limit 10 M ≈ first ≈ 5,900 guest instructions [D] — covers all non-idle code plus the first ≈ 900 idle iterations.
* **S2 steady idle (optional):** stop at `retire_impl` after K resumed stops (K from the `retired` counter at `0x8C1A86AC`/`0x8C1AC898`, e.g. 8,192), arm 2 M ≈ 1,200 guest instructions ≈ 300 poll iterations (a second arm is allowed once S1 is written).
* **What it can establish:** the executed-address sets and fetch order of the head and of the periodic idle loop, per binary. **What it cannot:** the exact 28 M-instruction sequence, window-end behaviour, D-cache, or any cycle/miss count; periodicity of the idle loop is an assumption to be checked on S2, not a given.
Do not propose unbounded capture: hard caps — 10 M instructions per arm, 160 MB host RAM, trace write ≤ 5 s, wall bound per stage.

## 7. Comparability levels
* **Level A (exact frozen binary):** achievable only after a `/pc` provider exists (A2, or A1 if a dcload image is obtained). The executed bytes are the frozen ones; verify with `sha256sum` of the ELF/CDI payload and the descramble comparison used in Gate 6.
* **Level B (derived binary):** needs source edits (checkpoint/asset/H5 paths) → new LTO partitioning and layout; **not byte-identical** to B16/L0B16. A G5-E0-style identity relink cannot restore code that changed. Required verification: same `.text` bytes for the hot set (`retire_impl`, bridge, decode, step, tick, dispatch) modulo relocated literals, same section order and 8 KiB-index residues, `native=0`, same `SNAPSHOT_H5_EXECUTION_COUNTS`, hash-pinned recipe. High risk of changing the very layout effect under study.
* **Level C (Gate 7A interpreter proxy):** no conclusions about the B16/L0B16 regression.
**Achievable level:** A (via A2), conditional on approval.

## 8. Relationship to G5-E0 and SH4Fit
G5-E0 (`SMA2_ICACHE_G5_E0_EXECUTION_20261017.md`, ELF `63f48c60…`, PASS) is a different ELF family (the interpreter-only Phase-1 image); not repeated. SH4Fit's `cachesim.replay` consumes ordered `FETCH` events (`ea`, optional `pa`, `cacheable`, `xfer` kinds; `sh4fit/cachesim.py`). A verified nullDC3 trace maps to it directly: `ea = pc`, `xfer` from the opcode and the next PC (delay-slot semantics are in the trace as separate records), P1 `0x8C…` cacheable. It would give executed addresses, I-cache index usage, temporal reuse, cross-function interference and B16-vs-L0B16 address differences. Limits: instruction fetches only, no data/store-queue/bus traffic, nullDC3's own timing, KOS tick contamination, and only the sampled windows. Model predictions must be kept apart from the PMU measurements (997,657 / 1,343,802; 24.8 M / 36.5 M cycles); R2 already failed its ±2 % gate with a guessed executed set (10.3× / 6.6× over), so a real trace is the missing input, and it must pass that same gate before any conclusion. No simulation was run here.

## 9. Recommended next gate
**Preferred — Gate 8A "/pc provider + B16 head sample" (needs separate approval; two sub-steps):**
1. *Smoke (smallest experiment):* implement and test the nullDC3 dcload HLE offline (host directory mapping, 22 syscalls, write support), unit-test it with a tiny KOS program (`dsptest.elf`-class fixture) — no SMA2.
2. *Live:* new media dir from the gold set (gold hashes, checkpoint `50282ea4…`, **no** `sma2_pmu.txt`), frozen **B16-LOWPMU-sel** first, Gate 5-style launch (real BIOS or REIOS ELF, `Dynarec.Enabled=0`, new port, no other session touched, single PID, TERM by PID only). Stages: (i) verify `CHECKPOINT_OK restored frame=200`, `SNAPSHOT_H5_IMMEDIATE_PORTABLE_EXACT_PASS`; (ii) breakpoint at `0x8C01BEFA`, verify state/`armed/retired`; (iii) arm 1,000,000 instructions (smoke), then if valid the 10 M head sample; (iv) pause, validate (Gate 5/7A checks + first PC `0x8C01BEFA`, expected function set: `runtime_bridge_interpret`, decode, step, `retire_impl`, no startup code). Failure/timeouts: checkpoint not found, breakpoint not hit in 120 s, H5 mismatch, any non-empty recorder error ⇒ STOP, no retry. Then L0B16 with the same procedure. Run B16 first (it is the smaller, no generated code); L0B16 only after B16 passes.
**Fallback — nullDC3 recorder enhancement first:** pause-on-limit and a raised cap (≥ 30 M) so one complete window (≈ 446-455 MB) is captured per binary, still on top of A2; or Level B only if A2 proves impossible, with the §7 verification.
Prerequisite list before any execution: approval for the nullDC3 change, a new media directory, (CDI only if the CDI route is chosen), a launch/validation script review, memory/disk budget (≥ 2 GB free for 10 M traces).

## 10. Unknowns / risks / stop conditions
* [U] REIOS and BIOS+CDI behaviour for this KOS ELF with a dcload HLE; dcload semantic edge cases (stat/opendir) used by KOS init; SCIF/dcload-serial route.
* [U] Idle-loop periodicity in the nullDC3 trace; KOS tick interleaving changing the sequence run to run (repeatability not yet shown even for Gate 7A).
* [U] Whether H5 immediate/endpoint writes (~636 KB each) behave under the HLE.
* Stop: any frozen hash mismatch, `CHECKPOINT_NOT_FOUND`, `retired != 0` at the start stop, trace containing startup code, active PMU use, or a recorder error reason.
No hardware/nullDC3 claim is made about cache misses: the nullDC3 trace is an executed-address record only.
