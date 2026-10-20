# Native-row eligibility per checkpoint and capture ordering (2026-10-19)

Offline only. No emulator or hardware launch, no repo source change, no renderer change. Evidence: `evidence/sma2_native_eligibility_probe_20261019/` (probe source, patch, build script, raw per-checkpoint output, summaries).

## Native-row eligibility (measured)
Method: a scratch copy of the host harness (`iw_main_cls.cpp`, not in the repo's source tree) installs a PPU presentation observer that calls the bridge's own `classify_fallback` (`src/dreamcast_native_pvr_bridge_contract.h:110-133`) on every scanline's DISPCNT, IO page and OAM. It then counts rows with reason 0 (the bridge's native-eligibility condition; `native_scanline` at `bridge.cpp:340-383`, `row_is_native_owned` at contract `:138`). The probe does not model the backend decline (`FallbackBackendDeclined`) or the actual PVR draw, so "eligible" is an upper bound on native rows.

Validation of the probe: for `m_2400` it gives `win=160 blend=160 eligible=0` on every frame, exactly what the hardware-side and nullDC3 runs reported (`forced_reasons=win:160,blend:160`).

| Checkpoint set | Result (10 VBlanks each, 12 for frame 200) |
|---|---|
| `pc_frame200_checkpoint.gbas` (cf2f2a80...) | **160/160 eligible every frame** (DISPCNT 0x3300, no window, no blend) |
| `g_599`, `g_999` (grid) | **160/160 eligible every frame** (DISPCNT 0x1600) |
| `m_2400`, `m_2990`, `m_5340` and all other 31 music/grid checkpoints | **0 eligible** on every frame: window on all 160 rows and blend on all 160 rows (2990 also mosaic) |

So only 3 of the 37 checkpoints can exercise the native path. The three hardware scenes (2400, 5340, 2990) cannot: they are fallback-only under the current classifier, so a PVR run on them tests fallback presentation only. This also explains why those scenes show 0 native rows in run 3/4.

## Capture ordering (source, partly unresolved)
Established:
- Guest: `frame_ready` -> `PVRGBAFrame` (submit) -> `capture_gameplay_front`: `pvr_wait_ready()`, `pvr_wait_render_done()`, `vid_waitvbl()`, then reads `pvr_get_front_buffer()` (`bridge.cpp:155-186`). `ready=0 render_done=0` are the waits' return values (0 = no timeout; `pvr_scene.c:374-420`), not state flags.
- KOS: the page flip happens in the VBlank handler when `render_completed` (`pvr_irq.c:112-131`). `pvr_get_front_buffer` returns the buffer that is displayed or is about to be (`pvr_misc.c:262-278`), so the guest capture reads the newly rendered frame.
- Emulator: `rend_vblank` queues `RenderFramebuffer`, which calls `Present()` and stores the frame via `runctl_present` (`Renderer_if.cpp:505-531`, `refsw2.cpp:54-55,299`). `Exit.FramePng` writes only the last stored frame at exit (`run_control.cpp:184-196`).

Not established:
- Whether the emulator's present at a VBlank sees the buffer after or before the guest's flip IRQ handler for that VBlank. So "capture then exit" could save frame N or N-1. Not determined from source.
- **Whether guest exit ends a run.** KOS `exit` under dcload calls the dcload EXIT syscall (`startup.S:123-134`), which only records `exited_` (`dcload_abi.cpp:194-196`; nothing reads `exited()`), then returns to the initial PR 0xac00043c (`reios.cpp:469`), which should print `REIOS: Exit` and end an automated run (`reios.cpp:387-398`). In run 3 and run 4 the failure path called `std::exit(1)` and the log shows **no `REIOS: Exit`**: the guest rebooted (second `REIOS: Booting up`) and ran to the 120-VBlank bound. So guest exit is observed not to end the run; the cause is not determined.

## Recommendation
- Do not rely on exit-after-capture plus `Exit.FramePng` until the exit behaviour is explained. The observed behaviour (reboot, then black final frame) contradicts it.
- A narrowly scoped opt-in emulator-side capture at a given presentation count is the deterministic route. It needs an emulator change (a source-level decision for you).
- For the native-composition milestone use frame 200 (resumed frame 201), which the classifier says is 160/160 eligible; `g_599`/`g_999` are alternates. A frame-201 validation ELF must be built with capture start 201 and the frame-200 checkpoint staged; the existing cross-scene ELFs are for the all-fallback scenes.
- Keep the refsw2 conversion model (`SMA2_NULLDC3_COLOR_PATH_ANALYSIS_20261019.md`) for emulator-only checks, not hardware acceptance.
