# nullDC3 SH-4 trace of the frozen B16 / L0B16 ELFs - Codex handoff

State at 2026-10-17 (Gates 4, 5, 7A, 7B, 8A, 8C, 8D, 8E). Everything below is a measurement or a source citation; hypotheses are labelled. No Dreamcast hardware was used for any of it.

## 1. What exists

* **nullDC3 branch** `feature/dcload-sh4-tracing` in the worktree `/tmp/nulldc3-sh4trace` (base `165775e5`, = `main`; commits `bf608d3b` recorder + RPCs, `dc746673` dcload `/pc` bridge, `dba2bb81` tools + guide; not pushed; the original checkout `/home/gpf/code/nulldc3` was not touched). It holds the SH-4 interpreter recorder and `trace.arm/disarm/status` RPCs, the REIOS dcload `/pc` bridge with its tests and KOS fixture, and the capture/validation/comparison tools (`tools/sh4trace/`). Read `docs/nulldc3/SH4_TRACE_DEBUGGING_GUIDE.md` first; it is self-contained. The worktree is in `/tmp`: if it is gone, the commits are in the shared repository of `/home/gpf/code/nulldc3` (`git branch feature/dcload-sh4-tracing`).
* **This repository:** the gate reports `docs/results/SMA2_*_20261017.md` and the evidence `docs/results/evidence/sma2_nulldc3_trace_20261017/` (scripts as originally run, logs, RPC transcripts, hashes). The raw traces and the fixture ELF are in `.../traces_not_committed/` (untracked on purpose, 16 MB each; hashes below).

## 2. Results

**Gate 8A (dcload `/pc` bridge).** Implemented opt-in (`Dcload.PcRoot`, `Dcload.PcWritable`; off by default), sandboxed, REIOS boot only. ABI verified from KOS source, the kos-tool dcload client/host, and disassembly of the frozen ELF. Host tests: `dcload.host`, about 260 checks, pass; negative controls (no symlink block, no `..` check, ignored writable flag) were caught. A KOS-only fixture (no SMA2 code) then ran in nullDC3 (REIOS ELF boot): **40/40 checks with a writable root, 34/34 read-only**. Report: `SMA2_GATE8A_DCLOAD_PC_BRIDGE_20261017.md`.

**Gate 8C (B16).** Frozen `B16-LOWPMU-sel.elf` booted unchanged, restored the frame-200 checkpoint (`CHECKPOINT_OK`, H5 immediate file == checkpoint), stopped once at the H5 window start `0x8C01BEFA`, start state validated, 1,000,000 consecutive SH-4 instructions recorded with zero drops. Validator: all checks PASS. Run 1 was stopped by the gate before arming because the expected start state was wrong (see guide section F); run 2 used the corrected, source-derived criterion. Report: `SMA2_GATE8C_B16_DEBUGGER_CAPTURE_20261017.md`.

**Gate 8D (L0B16).** Same procedure on `L0B16-LOWPMU-sel.elf` at `0x8C01FEA2`: identical guest start state, 1,000,000 instructions, zero drops, validator PASS (15/15). Report: `SMA2_GATE8D_L0B16_MATCHED_TRACE_20261017.md`.

**Matched comparison** (each trace symbolised with its own ELF; raw PCs never compared; `comparison.out`):

| Metric | B16 | L0B16 |
|---|---:|---:|
| SH-4 instructions captured | 1,000,000 | 1,000,000 |
| Guest retirements (`retire_impl` entries) | 319 | 273 |
| Bridge entries (`runtime_bridge_interpret`) | 22 | 19 |
| Interrupt entries / REIOS traps | 8 / 2 | 8 / 2 |
| Unique SH-4 PCs | 9,450 | 10,099 |
| Unique 32-byte lines | 911 | 981 |
| Unique direct-mapped 8 KiB indices (of 256) | 255 | 255 |
| Application (LTO) share | 78.9 % | 68.1 % |
| Library share (libc/libgcc/libstdc++) | 21.0 % | 31.8 % |
| KOS share | 0.12 % | 0.12 % |

The traces do not cover the same guest range: B16 reaches 319 guest retirements, L0B16 273. On the common prefix of 272 fully covered retirements L0B16 takes **about 13.5 % more SH-4 instructions** (999,597 vs 880,305; 3,675 vs 3,236 per guest instruction). The 1M traces cover the early, non-idle part of the H5 window (the window is about 28 M SH-4 instructions, 85.9 % idle polling per Gate 7B). **Do not generalise them to the 16,384-guest-instruction window.** Unique-line and index counts are footprints, not cache misses; no cache simulation has been run.

## 3. Hashes

| Artifact | sha256 |
|---|---|
| B16-LOWPMU-sel.elf | `60c1289c02082c809deabf901dbe4831ca04527062a3685248989617e850dc8f` |
| L0B16-LOWPMU-sel.elf | `f40fc020dcde51c9b882831f6289d36cee746fb7419c14b44d8f3ca1eb4eec57` |
| checkpoint `sma2_checkpoint.bin` (635,929 B) | `50282ea4be8bea13b9063a38438dcde0b0b330bf0f3b929edf525bb112af32e6` |
| ROM / BIOS / game.toml / save | `63d9fff0...a4f3` / `fd254772...6570` / `ecbb5dd7...f2a` / `d7f59620...38e73` (full values in the manifests) |
| emulator `nulldc3-emu` (Gates 8C and 8D) | `e51831cd640b4b39d7a83fac6c75ceba84dfbceccf292bb113af9563c3fd9eaa` |
| `b16_1m.n3it` (16,000,056 B) | `c42c680aa3de31a8bf241f9ab9f4289a8f0519633cc53df173c8ef7c6cad4939` |
| `l0b16_1m.n3it` (16,000,056 B) | `e5bdaa6e22faa4abf00b5257dfeeaf1724ac834b5a9e6fc20bd15658b56f1d7e` |
| `dcload_fixture.elf` (2,100,152 B) | `3dd604623f8f94c46a80476b0cc66c86c201660f5881447089d6db8ad436b23d` |
| `gate8c_client.mjs` | `ea044384b145194db64cde8ae6c61e0ade22d314dc2d4384153138672342a9ac` |
| `validate_trace8c.py` (preserved copy = after the Gate 8D interrupt-after-slot correction; the version used for the original Gate 8C report hashed `ff2b261038cd7b17e604ab70954950d44b7c4b9f85fff88b1493b6449a5c8c9a`) | `5dcda26ff159760a221a7b89d26b14220a48e1dfb83d1b4be65948bcb2f3be1e` |
| `gate8d_client.mjs` | `63f281802710c46c39949d17fc0cf1c819b1018450d69ded5b9076ac334e7a4f` |
| `validate_trace8d.py` (final version, with the interrupt-after-slot correction) | `ac38bebf037e5080b4b228b9fd2c988eacde893d201f3b19761668ca3c1c3ce4` |
| `compare_b16_l0b16.py` | `c98abadc5aac72dc88d2fbf7201cf1c8f6055c4fa8c1d30dc0c8de8c9d8bd785` |

Frozen manifests for the ELFs: `docs/results/evidence/sma2_low_perturbation_pmu_20261017/SHA256SUMS`; the gold media directory is `/tmp/sma2-parity-C-16384-media-20261016/` (hash-matches the gold manifest `sma2_aot_tail_hardware_20261017/timing/b01-B16/Q-B16-inputs.sha256`).

## 4. Evidence locations

`docs/results/evidence/sma2_nulldc3_trace_20261017/{gate8a,gate8c,gate8d}/` hold per run: the launch command (`launch.sh.record`, `emulator.cmdline`), preflight output, the client and validator as run, the full RPC transcript (`rpc.jsonl`), `start_state.json`, `client_result.json`, the console log, `trace_validation_full.out`, trace hashes and `cleanup_status.txt`. Gate 8C run 1 (blocked by the start-state check) is in `gate8c/run1_blocked/`. Note: the Gate 8C `trace_validation_full.out` was produced by the validator after the Gate 8D interrupt-after-slot correction (same result: valid, 8 interrupt entries). Files named `*.log` (console, client and ctest logs) sit in the evidence directory but are covered by this repository's `.gitignore`, so they are on disk and uncommitted. Offline host-test log of Gate 8E: `gate8e_ctest_host_only.log` (77 passed, 79 skipped, 0 failed of 156).

## 5. Reproduction commands

Follow guide section I. In short (placeholders in angle brackets; `NM` = `sh-elf-nm -S -C <elf>`):

```
# in the nullDC3 worktree on feature/dcload-sh4-tracing
cmake -S . -B <build>/emu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHAS_TA_LLE=ON -Wno-dev && ninja -C <build>/emu
python3 tools/sh4trace/check_inputs.py --manifest tools/sh4trace/manifests/sma2_l0b16_lowpmu_sel.json --elf <D>/L0B16.elf --media <D>/media --emulator <D>/nulldc3-emu
setsid -f <D>/launch.sh > <D>/emulator_console.log 2>&1        # launch.sh as in guide section C, port <P>
timeout 900 node tools/sh4trace/capture_at_breakpoint.mjs --manifest tools/sh4trace/manifests/sma2_l0b16_lowpmu_sel.json \
    --url ws://127.0.0.1:<P>/ws --out <D> --trace <D>/l0b16_1m.n3it --console <D>/emulator_console.log --media-dir <D>/media
kill -TERM <exact pid>
python3 tools/sh4trace/validate_trace.py <D>/l0b16_1m.n3it <D>/L0B16.elf --manifest tools/sh4trace/manifests/sma2_l0b16_lowpmu_sel.json --nm <D>/L0B16_nm_sizes.txt
```

## 6. Known limitations

* Instruction fetches only (PC and opcode); no data accesses, bus, store queue, cycles or cache outcomes. nullDC3 interpreter timing, REIOS and KOS interrupt cadence (8 entries per 1M) are part of the traces.
* B16 and L0B16 were captured once each; trace-to-trace variation is unmeasured. The two prefixes are assumed to be the same guest instructions (same checkpoint, deterministic guest); not independently proven.
* Headless operation together with the debugger was not exercised; the runs used a window (`DISPLAY=:0`).
* The bridge needs the REIOS boot path (no install hook for real-BIOS + CDI boot). The guest restarts after it returns (reboot loop); `Exit.OnGuestExit` did not end the fixture run.
* Origin classes (KOS / libc / application) come from the base link maps; the `-sel` ELFs differ from base by one literal word. About 0.7-1.0 % of instructions sit in unsized local labels.
* Counts of 1M instructions are samples of the early window; they say nothing about the idle-poll steady state or the window end.

## 7. Open hypotheses (not established)

* **H1 (unexplained):** L0B16-only formatting/copy activity - `_svfprintf_r` 4.7 %, `memmove` 2.6 %, `__ssprint_r` 1.4 %, `__ssputs_r` 1.1 % of the common-prefix instructions (about 10 %), absent from the B16 1M - explains part of the +13.5 % instructions per guest instruction. Its cause (a different code path or logging in L0B16, or the same path at a different time) is **not** known, and it is **not** yet an explanation of the hardware cache regression.
* **H2:** the hardware cache regression comes from instruction-fetch footprint/conflicts in the larger L0B16 working set (981 vs 911 lines, 10,099 vs 9,450 PCs). Footprint is necessary context only; misses need a model.
* **H3:** the early non-idle front (about the first 320 guest instructions) is not representative of the idle-dominated window.

## 8. Recommended next experiments (each needs its own approval)

1. Locate the call sites of `_svfprintf_r`/`memmove` in the L0B16 trace (caller by return address: walk the trace for the `jsr`/`bsr` sequences into them) and compare with the source difference between the two builds; decide H1 before anything else.
2. Steady-state samples at the same guest point in both binaries (stop at `retire_impl` after the same `retired` count, e.g. 8,192; 2 M cap per arm) so the idle loop is covered.
3. Only then a cache simulation (SH4Fit `cachesim.replay`) of both traces with an explicitly stated model (8 KiB, 32 B lines, direct mapped, P1 cacheable), its predictions kept apart from the PMU measurements (997,657 / 1,343,802 misses; 24.8 M / 36.5 M cycles). Its R2 precedent failed a +/-2 % gate with a guessed executed set; the trace is the missing input, not proof.
4. A repeat capture of each binary to measure trace-to-trace variation.

## 9. Gate 8E offline verification (no emulator run of SMA2)

* Host-only ctest (names in `/tmp/nulldc3-sh4trace-gate8e-20261017/host_only_test_names.txt`, 156 tests): **77 passed, 79 skipped (external data missing), 0 failed**, run twice. The 128 guest-booting tests of the 284 were not selected. Separately `ctest -R 'sh4.trace|dcload'`: 5 of 5 passed (sh4.trace.interpreter, sh4.trace.arm, sh4.trace.capture_client_mock, dcload.host, and sh4.trace.immediate_start). **Disclosure:** `sh4.trace.immediate_start` is a guest-booting test (it launches the emulator on the in-tree `dsptest.elf`, about 1 s, own work directory); it was included by the `sh4.trace` pattern by mistake, contrary to the "no guest launches" rule of Gate 8E. It involves no SMA2 binary, no frozen ELF and no `/pc` root.
* Static: `static_check.py` on the fixture ELF: all checks pass; `check_output.py`: synthetic good log exit 0, bad log exit 1; `validate_trace.py` on the preserved traces: both `TRACE VALID`; `compare_traces.py` reproduces `comparison.out` (only the title and one wording line differ); `git diff --check` clean on both repositories' committed files; trace hashes unchanged.
* The capture client was exercised only against the mock debugger (`sh4.trace.capture_client_mock`).
