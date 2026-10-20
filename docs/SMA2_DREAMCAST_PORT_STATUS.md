# SMA2 Dreamcast Interpreter Port Status

Updated 2026-10-15.

## Demonstrated milestones

| Milestone | Status | Evidence |
|---|---|---|
| Interpreter starts | PASS | Physical console log records interpreter backend and runtime initialization. |
| SMA2 ROM boots | PASS | The local 4 MiB ROM and 16 KiB BIOS load; guest reaches the game startup sequence. |
| GBA framebuffer renders | PASS | User directly observed the ROM's “Nintendo Presents” splash on the Dreamcast display. |
| Title/game transition | PASS | User directly observed the single-player/multiplayer menu and Super Mario World title screen; the current run has advanced through the first splash to the second. |
| Controller works | PASS for menu navigation | User reports D-pad up/down moved between menu options and a button press advanced through the menus. Latest hardware trace confirms `rtrig=243` with no face-button bits maps to `KEYINPUT=0x03fe` (GBA A pressed). |
| Gameplay entered | NOT CONFIRMED | The Super Mario World title screen is visible; no level gameplay yet. |
| Audio output | PASS | User directly hears game music through KOS PCM. They report it sounds slow/odd along with the game; fidelity and sustained playback remain unmeasured. |
| Sustained gameplay tested | NOT TESTED | No completed sustained play session has been recorded. |

## Current build

Build with `tools/build_dreamcast.sh` after sourcing the KOS environment (the
script sources it itself). The interpreter is forced on, no generated cart
corpus is linked, and KOS compatibility adaptations are staged in the ignored
`build-dreamcast/gbarecomp-overlay/` directory. The separate GBARecomp checkout
is not modified.

The latest ELF is `build-dreamcast/SuperMarioWorldRecomp.elf`; the linker map is
`build-dreamcast/SuperMarioWorldRecomp.map`. `sh-elf-size` reports 1,145,909 B
text, 8,176 B data, and 1,145,984 B BSS, 2,300,069 B total. Its SHA-256 is
`f2b3816dab86d9e2a898d20386f32547dbee6feab4862e0a28481c31902bd36f`. The ROM is loaded
from `/pc/roms/super_mario_world_usa.gba`; BIOS from
`/pc/gbarecomp/bios/gba_bios.bin`. The framebuffer is centered at 240x160 on a
320x240 RGB565 KOS framebuffer.

The latest hardware launch used the shared lock wrapper with a 180-second
timeout. It includes the Start+A+B+X+Y exit chord, every-10-presented-frame
guest telemetry, a KOS mono PCM stream, and right-trigger-to-GBA-A mapping.
The user directly observed the second startup splash in this run, then rebooted
back to dcload. The KOS
`HostWindow::present()` diagnostic printed `presents=1` and `presents=60`,
confirming repeated video handoffs. No guest frame telemetry line appeared in
`/tmp/sma2-video-present-counter-20261015.log`, so the runtime counter remains
unresolved. The launcher ended and released the hardware lock. Earlier, the
user directly heard game music and reported that it sounds slow/odd along with
game execution. The earlier hardware input trace confirms the trigger alias.
## Remaining work

1. Verify title music and sound effects are audible and stable.
2. Capture frame telemetry and confirm D-pad/A/B controls enter and operate a
   level; record performance.
3. Measure full runtime memory use and headroom on hardware. The ELF size alone
   is not a full memory guarantee.

## Physical run record

Initial successful run log: `/tmp/sma2-dc-fixed-asset-args.log`. The current
ELF must be launched only through
`/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh` using a unique
`MGBA_DC_LOCK_TASK`. No hardware configuration or sibling repository was
modified for this port.

## Native PVR pixel validation update — 2026-10-19

The selected frame-201 native BG/OBJ path passed physical Dreamcast acceptance
with 160 native rows, zero fallback, and zero raw mismatches across 38,400
RGB565 pixels. The SMA2 mixed-row fixture passed with 152 native rows and 8
software-fallback rows, also with zero raw mismatches. These results cover the
selected scene and ownership boundary; they do not establish general renderer
correctness or full CPU/device/IRQ/clock equivalence.

nullDC3 independently matched frame 201 at 0/38,400 after applying a
source-backed emulator color model selected from each row's logged ownership:
RGB565 texture conversion on fallback rows and ARGB1555 palette conversion on
native rows. The raw mismatch count is retained as a diagnostic. Never apply
the nullDC3 model to Dreamcast hardware comparisons, and do not apply the
fallback RGB565 model to native palette rows. The reusable fail-closed
comparator is [`tools/compare_sma2_nulldc3_pvr.py`](../tools/compare_sma2_nulldc3_pvr.py).

Full CPU/device/IRQ/clock fingerprint equivalence remains incomplete; pixel
equivalence is not a substitute. The closure report and next checkpoint gate
are in
[`SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md`](results/SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md).
