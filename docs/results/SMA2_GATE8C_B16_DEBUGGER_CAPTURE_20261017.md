# Gate 8C - Frozen B16 debugger capture

Verdict: **PASS** (with the start-state criterion corrected in run 2, see below; one qualification in the last section).

Evidence: `/tmp/nulldc3-sh4trace-gate8c-20261017/` (`SHA256SUMS.txt`, `preflight.out`, `launch.sh.record`, `emulator.cmdline`, `rpc.jsonl` = full WebSocket transcript, `start_state.json`, `b16_1m.n3it` + `.sha256`, `trace_validation.out`, `emulator_console.log`, `cleanup_status.txt`; run 1 in `run1_blocked/`).

## Identity and configuration (measured)
- B16-LOWPMU-sel.elf sha256 `60c1289c02082c809deabf901dbe4831ca04527062a3685248989617e850dc8f`; checkpoint `50282ea4be8bea13b9063a38438dcde0b0b330bf0f3b929edf525bb112af32e6`; ROM/BIOS/game.toml/save = gold hashes; fresh media copy per run, no `sma2_pmu*` (PMU mode inactive).
- Emulator `nulldc3-emu` sha256 `e51831cd640b4b39d7a83fac6c75ceba84dfbceccf292bb113af9563c3fd9eaa`, rebuilt from the tree, contains the Gate 4 recorder and the Gate 8A bridge (`nm`); REIOS ELF boot, `Dynarec.Enabled=0`, `Dcload.PcRoot` = the media copy, port 55611.

## Run 1 (BLOCKED by the gate, no capture)
At the start breakpoint R15/cycles/interpreter_retired were `0x0809B278` / 56,424,200 / 1 instead of the Gate 7B values `0x0809B276` / 56,424,198 / 0. Cause (source): `interpreter_instruction_retired` increments the counter then calls `retire_impl`, and the start timestamp is inside `retire_impl` at index 0, so the first window instruction has already executed at the start RA (`sma2_snapshot_h5_diag.cpp:494-511,648-652`). The expectation was changed to the observed, source-explained values on your instruction to proceed; run 2 used the corrected criterion. Run 1's emulator was ended by exact PID.

## Run 2 start state (all checks PASS before arming)
Breakpoint `0x8C01BEFA` stopped once by `breakpoint`; bytes `50e7`; checkpoint restored (immediate file == checkpoint hash; one `CHECKPOINT_OK frame=200`); H5 armed=1 complete=0 retired=0; interpreter_retired=1; native calls=0; R15 `0x0809B278`; CPSR `0x6000003F`; cycles 56,424,200; VBlank 201; IRQ entries 192; recorder `off`.

## Capture and validation (`trace_validation.out`)
Arm 1,000,000 instructions, resume, `complete`, written, then paused through the protocol. Trace sha256 `c42c680aa3de31a8bf241f9ab9f4289a8f0519633cc53df173c8ef7c6cad4939`, 16,000,056 B.
- Header complete, interpreter mode, 1,000,002 records (start + 1,000,000 + end), dropped 0, sequence contiguous.
- First PC `0x8C01BEFA`, opcode `0xE750` = ELF word.
- 999,998 instructions at ELF file-backed PCs: 0 opcode mismatches. The other 2 are REIOS HLE traps (opcode `0x085B` at `0x8C001006`, RAM dcload/REIOS area), which return to PR; not ELF code.
- 154,975 delayed branches: slot at pc+2 every time, no branch in a slot; 130,803 direct targets match their displacement.
- 8 interrupt entries at `0x8C0E6450` (`_vma_table_600`, KOS VBR+0x600 vector; a local label, absent from sized `nm`). 2 of them follow a not-taken `bf/s` (slot not executed) and the code resumes at pc+2 later, as required. 0 unexplained discontinuities.
- Path: `retire_impl` entered 319x, `runtime_bridge_interpret` 22x, `ThumbDecoder::decode` 320x, `Interpreter::step` 311x, `dispatch_resolve` 22x; `capture_named` 0x. The sample ends mid-step.
- Top functions: ThumbDecoder::decode 14.9%, runtime_bridge_interpret 13.2%, `_findenv_r` 11.9%, Interpreter::step 11.0%, strncmp 5.0%, store_interp_into_arm_cpu 4.9%, record_and_log_miss 3.9%.

## Guest retirement
The sample contains 319 `retire_impl` entries (the index-0 call, which holds the start timestamp, was already in progress at the first record) and 320 decodes: about 320 guest instructions, i.e. roughly 3,100 SH-4 instructions each at the start (idle/poll is not yet reached). After the pause the H5 counter read 1,160 retired (pause latency, emulation ran on) so that value is not the sample size. 1M SH-4 instructions is not a frame and not the 16,384-instruction window (about 28 M).

## Qualification
The sample contains KOS code (8 vblank/timer interrupt entries, 2 REIOS traps: `iso_vblank` and an IRQ-driven thread path appear in the trace), `_findenv_r`/`strncmp` (likely config/env lookups) and nullDC3 timing; it is not a pure model of the hardware run. Memory and bus traffic are not recorded.

## Cleanup
Run 2 emulator PID 1041726 ended by SIGTERM to that exact PID, port closed (`cleanup_status.txt`); no pkill/killall; no other process touched; nothing committed; L0B16, SH4Fit and cache simulation not run.
