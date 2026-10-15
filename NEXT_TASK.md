# Next Task: Continue Dreamcast Interpreter Bring-up

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

## Repository boundaries

- Do not push without separate authorization.
- Do not initialize, update, repin, or modify the GBARecomp checkout.
- Keep SMA2-specific platform integration and compatibility overlay here.
- Do not claim controller, audio, or sustained-play success without direct
  evidence.
- Detailed current observations and limitations are in
  [`docs/SMA2_DREAMCAST_PORT_STATUS.md`](docs/SMA2_DREAMCAST_PORT_STATUS.md).
