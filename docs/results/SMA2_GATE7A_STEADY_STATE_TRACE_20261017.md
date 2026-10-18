# SMA2 Gate 7A — steady-state SH-4 trace validation — 2026-10-17
**Verdict: PASS (with a representativeness limitation, §9).** One guest launch, one 100,000-instruction capture, no retries, no source/ELF/CDI changes, no commits.
Evidence: `/tmp/nulldc3-sh4trace-gate7a-20261017/` (`SHA256SUMS.txt`). Gate 5 and Gate 6 evidence untouched. Labels: [V] verified, [D] derived, [U] unverified.

## 1. Preflight [V]
ELF `e81c0f65ee5d6bb75a523b3f2253ce6d2d5af3b947b8ca4c5bf8f1a8e45c5e03`, CDI `fc1b2426d7d4f9ec1fa9dbd227b5299dfb94b5ed1ec6410421ea7d2e5fb7f65e`, nullDC3
`49720442a368a919ee9e1c6eaec7feb57485068957d3a13f9e5e18d8a7ed9fa8`. ELF bytes at `0x8C045148`, `0x8C02ECEC`, `0x8C046B78` are `862f962f` (opcodes 0x2F86, 0x2F96); data symbols `g_cpu 0x8C195080`,
`g_runtime_cycles 0x8C19500C`, `g_runtime_vblank_starts 0x8C190D60`, `g_runtime_irq_entries 0x8C19DAD8`, `g_irq_nest_depth 0x8C22F434` confirmed in the nm output. Gate 6 report:
`docs/results/SMA2_STEADY_STATE_CAPTURE_POINT_ANALYSIS_20261017.md`. BIOS files present; no nullDC3 process, no 55xxx listener. Debugger RPC/recorder as Gate 4 (arm needs a stop; categories enabled by `breakpoints.setCategoryStates`).
**Search limits (derived, before launch):** hardware logs show ≈37 k guest instr/s and ≈3.4 cycles/instr, so a 280,896-cycle frame ≈ 2 s on hardware; Gate 3C ran nullDC3 at ≈2.5× real time. Target `g_runtime_vblank_starts ≥ 10` (≈ 20 s hardware / ≈ 10 s emulator expected); bounds: 300 s wall, 150 stops, 60 s per stop (>10× margin).

## 2. Launch [V]
`launch.sh.record`: `DISPLAY=:0 /tmp/nulldc3-gate4-emu/nulldc3 --portable …/gate7a-20261017/portable --config audio:backend=none , config:Dynarec.Enabled=0 , Debugger:Enabled=yes , Debugger:StartPaused=yes , Debugger:Port=55546 , config:Exit.AfterSeconds=1200 SMA2Trace.cdi`
(Gate 5 recipe, new port). Real BIOS (`dc_boot.bin`/`dc_flash.bin` copied into `portable/data`). One instance, PID **1020732**, own process group/session; first and only launch, no configuration error. Client: `gate7a_client.mjs` (`a462f955…`, frozen copy kept), every RPC in `rpc.jsonl`, every stop in `stops.jsonl`.

## 3. Frame-boundary evidence [V]
Breakpoint `0x8C045148` (`HostWindow::present .part.0`) hit 10 times; every stop: reason `breakpoint`, SH-4 PC `0x8C045148`, id 1. `g_runtime_vblank_starts` rose 1→10 by exactly one per stop (9 changes between 10 stops), i.e. **one present per GBA frame** (the two candidate present paths cannot be told apart from this; only the count per frame is established). Cycle deltas per frame 280,896 / 280,896 / 281,429 / 280,362 / 280,897 / 281,355 / 280,437 / 280,896 / 311,733 (the last frame is ≈ 30 k cycles longer — see §8). First stop at 35.8 s wall after client start (BIOS/KOS/asset startup), then ≈ 2.2 s wall per frame; stage 1 total 55.5 s.

## 4. Guest-state progression [V]
| stop | vblank | cycles | guest R15 | CPSR | IRQ entries |
|---|---|---|---|---|---|
| 1 | 1 | 280,897 | 0x0809E2A8 | 0x8000003F | 0 |
| 2 | 2 | 561,793 | 0x0809E29A | 0x0000003F | 0 |
| 3 | 3 | 842,689 | 0x0809E330 | 0x2000003F | 0 |
| 4 | 4 | 1,124,118 | 0x0809E1CC | 0x8000003F | 0 |
| 5 | 5 | 1,404,480 | 0x08008118 | 0x0000003F | 0 |
| 6 | 6 | 1,685,377 | 0x0809E30E | 0x0000003F | 0 |
| 7 | 7 | 1,966,732 | 0x0809E1CC | 0x8000003F | 0 |
| 8 | 8 | 2,247,169 | 0x0809E1C8 | 0x8000003F | 0 |
| 9 | 9 | 2,528,065 | 0x080073A0 | 0x0000003F | 0 |
| 10 | 10 | 2,839,798 | 0x0809DFC2 | 0x0000003F | 1 |
CPSR 0x..3F = System mode, Thumb. Cycles and VBlank advance with guest execution (not wall time): real GBA emulation of the SMA2 ROM in its first ten frames (boot/initialisation code at `0x0809Dxxx-0x0809Exxx` and `0x08007xxx-0x08008xxx`; the DMA3 poll region of `SMA2_PC_GAMEPLAY_CHECKPOINT_BRINGUP_20261016.md` appears at `0x0809E1C8-0x0809E1EA`).
**Frame choice:** frame 10 (first stop with vblank ≥ 10 and ≥ 3 observed vblank changes). Not the frame-200 reference (not required for 7A, and a cold-boot frame is not the saved checkpoint).

## 5. Interpreter-step breakpoint [V]
Frame breakpoint removed, breakpoint added at `0x8C046B78`, resumed: stopped (stopId 14, reason `breakpoint`, id 2) with SH-4 PC `0x8C046B78`, memory bytes `862f962f`. Guest state at the stop: R15 `0x0809DFC2` (ROM word `0x4770` = `bx lr`, preceded by `svc 11` at `0x0809DFC0`), CPSR `0x0000003F`, cycles 2,839,798, VBlank 10, SP `0x03007DBC`, LR `0x08002B73`.

## 6. Recorder lifecycle [V]
`trace.arm {sh4, path, 100000}` → `armed`, recorded 0, no file → breakpoint removed → resume → `limit-reached` (recorded 100000, written false; 51 ms wall) → pause → `complete`, written true, reason "". The guest was at R15 `0x08000530` (the frame hand-off poll), cycles 2,855,657 after the pause (+15,859 cycles beyond the stop).

## 7. Independent validation (`validate_trace7a.py`, `trace_validation.out`) [V] — RESULT: TRACE VALID
Header v1, complete(2), interpreter(1), limit 100000, count 100,002, dropped 0; size 1,600,056 B = 24 + 16·100,002; types 1/100,000/1; sequences 1..100,000 contiguous; start seq 0, end seq 100,001 opcode 0; no overflow/backend-exit.
First record PC `0x8C046B78`, opcode `0x2F86` (= ELF word); second `0x8C046B7A`/`0x2F96`; last `0x8C02B384`/`0x5013`. All 100,000 PCs lie inside the ELF image and every opcode equals the ELF's word at that PC (0 mismatches); 12,940 delayed branches, every one followed by its slot at PC+2.
No PC outside the ELF text: **no BIOS/low-RAM execution**. KOS code executed: `irq_handle_exception` 59, `thd_timer_hnd` 72, `thd_schedule` 199, `krn_stack` 92 — 422 instructions (0.42 %), first at seq 65,151: one KOS timer tick/context switch inside the window.
**Trace SHA-256 `a06e4e36065ea6513517d38cb6964484aed534ea0a0191fcbdd28bf400959efd`**, path `/tmp/nulldc3-sh4trace-gate7a-20261017/sma2-steady-100k.n3it`.

## 8. Guest steps and SH-4 ratio [V counts, D interpretation]
Entries to `runtime_force_interp_step`, `runtime_should_yield`, `Interpreter::step` and `ThumbDecoder::decode` are each **10**; `ArmDecoder::decode` 0; `runtime_irq` 0; `step_once` 9 + the one in progress. Source (`runtime_arm_default_aborts.cpp:766-`) gives one decode and one `Interpreter::step` per non-yielding call, so the window holds **10 Thumb guest steps** (9 complete + the 10th, cut by the limit).
Steps 1-9: 18,792 SH-4 instructions, **min 1,891 / median 1,996 / mean 2,088 / max 2,344 per guest instruction** (`ordinary_steps_profile.txt`; profile: `runtime_force_interp_step` 23.6 %, `ThumbDecoder::decode` 22.6 %, `Interpreter::step` 9.9 %, `run_game` 6.9 %, `store_interp_into_arm_cpu` 6.8 %, `runtime_tick.part.0` 5.6 %, `GpioPort::active` 4.6 %, `GbaBus::read16` 3.8 %, `step_once` lambda 3.5 %, `runtime_should_yield` 3.3 %). This matches the Gate 6 estimate (≈1.5-2.5 k) [V]. No fixed ratio is assumed: step 10 is 81,208+ instructions.
**Step 10 (81.2 % of the trace):** the guest instruction is an `svc`: it reaches `runtime_swi` (BIOS HLE), which then performs **383 halfword read/write pairs** (`bus_read_u16`→`GbaBus::read16`+`resolve_offset`, `bus_write_u16`→`GbaBus::write16`, ≈ 206 SH-4 instructions per unit). The trace ends mid-copy (383 reads vs 382 writes), so the SWI's total length is > 81 k. Which SWI (the ROM has `svc 11` = CpuSet at `0x0809DFC0`) is [U]; the 311,733-cycle frame 10 is consistent with a bulk BIOS copy but is not proof.

## 9. Suitability and limits
* Active interpreter execution [V]: yes — dominant functions are the force-interp step path and GBA bus accessors; no startup/asset/CD code and no present/render work in the window (`present hook`/`HostWindow::present` entries 0).
* **Representativeness is weak:** one HLE-BIOS SWI bulk copy is 81 % of the window; ordinary dispatch is 18.8 k instructions (≈ 9 guest steps); no idle-poll activity (step-length periodicity match 0.125) — idle polling is not captured (the guest reached the `0x0800052E` poll only after the capture). Frame 10 is early boot, not gameplay.
* Footprint [D]: whole trace 3,442 unique instruction addresses / 342 32-byte lines (10.9 KB); ordinary steps 2,610 addresses / 259 lines; the SWI loop 1,780 / 171.
* A long enough window for locality analysis of the ordinary path needs ≈ 2 k SH-4 instructions per guest instruction: ≈ 2 M instructions per 1,000 guest steps; the recorder maximum (10 M) ≈ 5,000 steps (≈ 0.3 of the B16 16,384-instruction window).
* **Repeatability: unverified [U].** Needed experiment (Gate 7B candidate): launch the same configuration twice with identical CDI/binary/BIOS/RTC (`config:Dreamcast.RTC` fixed), stop at the same vblank (10) and guest-step breakpoint, capture the same N, compare trace SHA-256 and the stop-state tuples (cycles, R15, IRQ entries); a KOS timer tick (422 instructions here) is the likely source of divergence.
* Not a B16/L0B16 experiment; no cache simulation; nullDC3 executed-PC traces say nothing about Dreamcast I-cache hits, misses or cycles.

## 10. Cleanup [V]
PID 1020732 verified (cmdline contains the gate's portable dir, same PGID/SID), `kill -TERM` to that PID only (no pkill/killall), exited within 1 s; port 55546 free; no other nullDC3 process touched or present. Trace hash unchanged after shutdown. Artifact hashes: `SHA256SUMS.txt` (20 entries).
