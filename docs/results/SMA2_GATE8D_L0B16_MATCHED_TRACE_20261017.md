# Gate 8D - Matched L0B16 SH-4 trace

Verdict: **PASS** (single capture; trace valid; comparison below). Evidence: `/tmp/nulldc3-sh4trace-gate8d-20261017/` (`SHA256SUMS.txt`, `preflight.out`, `launch.sh.record`, `emulator.cmdline`, `rpc.jsonl`, `start_state.json`, `client_result.json`, `l0b16_1m.n3it`, `trace_validation.out`, `comparison.out/.json`, `emulator_console.log`, `cleanup_status.txt`). Gate 8C B16 evidence: `/tmp/nulldc3-sh4trace-gate8c-20261017/`.

## 1. L0B16 identity (measured)
- `L0B16-LOWPMU-sel.elf` sha256 `f40fc020dcde51c9b882831f6289d36cee746fb7419c14b44d8f3ca1eb4eec57` = the frozen manifest line (`sma2_low_perturbation_pmu_20261017/SHA256SUMS`). Checkpoint `50282ea4...`, ROM/BIOS/game.toml/save gold hashes, fresh media copy, no `sma2_pmu*`. Emulator `nulldc3-emu` `e51831cd...` (recorder + `/pc` bridge, same binary as Gate 8C). REIOS ELF boot, interpreter, port 55612.
- Bytes in the ELF (objdump): `retire_impl` 0x8C01FE58 `862f 962f`; window start 0x8C01FEA2 `50e7`; window end 0x8C01FF8C `50e7`.
- L0B16 state addresses were resolved from its own `nm` (not copied from B16): g_cpu 0x8C145EAC (0xB4 B, R15 at +0x3C as in B16), cycles 0x8C145E98, vblank 0x8C145FD8, IRQ entries 0x8C1BAB10, iret depth 0x8C24EE40, H5 armed 0x8C1A988D / complete 0x8C1A988C / retired 0x8C1AC898, interpreter_retired 0x8C1A9774, native calls 0x8C1B0B44. The L0B16 source is the same H5 code as B16 (`sma2_snapshot_h5_diag.cpp:494-511,648-652`), so the Gate 8C corrected criterion applies; it was not relaxed.

## 2. Checkpoint and window-start state (all 14 checks PASS before arming)
Stopped once by the breakpoint at 0x8C01FEA2; bytes `50e7`; immediate file == checkpoint hash; one `CHECKPOINT_OK frame=200`; H5 armed 1, complete 0, retired 0, interpreter_retired 1; native 0; R15 0x0809B278; CPSR 0x6000003F; cycles 56,424,200; VBlank 201; IRQ entries 192. Identical to the B16 start state (the guest state is identical; only the SH-4 layout differs).

## 3. Trace identity
`l0b16_1m.n3it`, 16,000,056 B, sha256 `e5bdaa6e22faa4abf00b5257dfeeaf1724ac834b5a9e6fc20bd15658b56f1d7e`. B16 (Gate 8C): `c42c680aa3de31a8bf241f9ab9f4289a8f0519633cc53df173c8ef7c6cad4939`. Exact-PID cleanup (PID 1042892, SIGTERM, port closed); no retry; nothing else touched.

## 4. Validation (`trace_validation.out`: 15 PASS, 0 FAIL)
Complete, interpreter mode, 1,000,002 records (start + 1,000,000 + end), dropped 0, contiguous sequence. First PC 0x8C01FEA2, opcode 0xE750 = ELF word. 999,998 ELF-backed instructions: 0 opcode mismatches; 2 REIOS HLE traps (opcode 0x085B at 0x8C001006, non-ELF) classified separately. 151,020 delayed branches: slot at pc+2 always; 126,474 direct targets match except one that is an interrupt entered after the slot (it later resumes at the branch target 537 records on). 8 interrupt entries at `_vma_table_600` 0x8C0E999C (7 after ordinary instructions, 1 after a delay slot). 0 unexplained discontinuities. `runtime_bridge_interpret` entered 19x, `retire_impl` 273x, `capture_named` 0x. Guest instructions in the sample come from trace evidence: 273 `retire_impl` entries / 273 decodes. The Gate 8C validator got the same interrupt-after-slot fix and still reports B16 valid (8 interrupt entries, 0 unexplained).

## 5. Matched B16 / L0B16 comparison (`comparison.out`; symbols from each ELF separately; names normalised; no raw PC compare)
| metric (1M SH-4 instr each) | B16 | L0B16 |
|---|---|---|
| guest `retire_impl` entries | 319 | 273 |
| ThumbDecoder::decode / Interpreter::step entries | 320 / 311 | 273 / 264 |
| `runtime_bridge_interpret` entries | 22 | 19 |
| interrupt entries / REIOS traps | 8 / 2 | 8 / 2 |
| unique SH-4 PCs (ELF-backed) | 9,450 | 10,099 |
| unique 32-byte lines | 911 | 981 |
| unique direct-mapped 8 KiB indices (bits 12:5) | 255 of 256 | 255 of 256 |
| origin: application (LTO) | 78.87 % | 68.09 % |
| origin: toolchain libc/libgcc/libstdc++ | 21.01 % | 31.79 % |
| origin: KOS (libkallisti) | 0.12 % | 0.12 % |
| origin: REIOS trap | 0.0002 % | 0.0002 % |

Guest progress at the 1M boundary differs: B16 reaches 319 guest retirements, L0B16 273 (B16 is 46 guest instructions, 16.8 %, further). The two traces therefore cover different guest ranges. Common comparable prefix: the first 272 completely covered guest retirements (retire_impl entries 1..272; decodes 273, bridge entries 19 in both). In that prefix B16 needs 880,305 SH-4 instructions and L0B16 999,597 (+13.5 %; 3,236 vs 3,675 per guest instruction); interrupts 7 vs 8. Unique PCs/lines/indices are the same as in the full traces (all of them occur inside the prefix).
Function mix (prefix, B16 vs L0B16): ThumbDecoder::decode 14.5 vs 12.7 %, `_findenv_r` 13.5 vs 11.9 %, `runtime_bridge_interpret` 12.8 vs 11.1 %, Interpreter::step 9.6 vs 8.4 %, strncmp 5.7 vs 5.0 %, store_interp_into_arm_cpu 4.7 vs 4.2 %. Present only in L0B16 (B16 has none in its 1M): `_svfprintf_r` 4.7 %, `memmove` 2.6 %, `__ssprint_r` 1.4 %, `__ssputs_r` 1.1 % (about 10 % of its instructions). The cause of this formatting/copy work (a different code path or logging in L0B16, or a different timing of the same path) is not established here.

## 6. Limitations
- Instruction fetches only; no data/store-queue/bus traffic, no cycles; nullDC3 interpreter timing and KOS interrupt cadence (8 entries in each sample) are part of the traces; the early window (the first ~320 guest instructions) is the non-idle front, not the 28 M-instruction idle-dominated window.
- Unique-line and index counts are footprints, not misses; 911-981 lines over 256 sets says nothing about conflicts without a model. No cache simulation was run.
- Guest identity of the two prefixes is assumed from the identical checkpoint and deterministic guest code; interrupt timing could shift the SH-4 count. Origins come from the base link maps (the `sel` ELFs differ from the base by one literal-pool word); about 0.7-1.0 % of instructions sit in unsized local labels (shown as "(label)") and 1,152 KOS instructions are mostly the interrupt path.
- Single capture per binary; no repeat run to measure trace-to-trace variation.

## 7. Recommendation
Do not run SH4Fit/cache simulation on the 1M heads as if they represented the 16,384-instruction window. Next gate (needs your approval): a steady-state sample from each binary at the same guest point (arm at a `retire_impl` stop after the same retired count, e.g. 8,192, with the 2 M cap) so the idle poll loop that makes up about 86 % of the window is covered, then, only with an explicit model, a cache simulation of both traces, with unique-line/index counts used only as inputs. Also investigate the L0B16-only `_svfprintf_r`/`memmove` path first, since it changes the fetch mix.
