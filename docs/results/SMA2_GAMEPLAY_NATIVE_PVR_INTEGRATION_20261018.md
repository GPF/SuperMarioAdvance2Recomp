# SMA2 Gameplay Native PVR Integration

Original investigation: 2026-10-18. Implementation update: 2026-10-19.

## Verdict

**Superseded investigation verdict (2026-10-18): BLOCKED.** The
frame-200 checkpoint is a usable starting state for a future frame-201 test,
but connecting SMA2's live PPU to mGBADC's renderer is not a thin state-copy
adapter. The renderer consumes mGBA's internal software-renderer state and
callback lifecycle. A correct bridge needs a persistent mGBA shadow renderer,
per-line synchronization, explicit fallback-buffer ownership, and checkpoint
restore invalidation. The existing checkpoint commit path did not notify the
PPU presentation observer after restore. The update below records the
cross-runtime bridge implementation authorized on 2026-10-19. At the time of
the original implementation report, no hardware was launched; the later
acceptance result is recorded at the end of this document.

The synthetic BG0 result remains valid for its isolated fixture only: 160
lines, 160 display lines, zero fallback rows, and 0/38,400 RGB565 mismatches.
It does not validate SMA2 gameplay rendering.

## Checkpoint selection and frame semantics

Selected candidate: `docs/results/evidence/sma2_pc_gameplay_checkpoint_20261016/pc_frame200_checkpoint.gbas`, SHA-256
`cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188`.
It was produced after 200 frames of actual ROM execution and contains the
completed frame-200 framebuffer in its DEV PPU payload. The saved boundary is
a VBlank handoff; restored execution proceeds into frame 201. The prior bring-up
report describes the saved image as a rendered level/attract scene, not
interactive gameplay. This is a better live-scene candidate than a reset frame,
but it is not the requested second-game splash/menu.

The old `frame200_latched.ppm` has SHA-256
`beff1988e0155819d602147f450c7a99702294629f1f0168631289011a7cd548`; research
found it unattributed to the checkpoint's producer state, so it is **not** an
acceptable pixel oracle. The checkpoint's saved latch documents frame 200,
not the new frame 201 produced after restore. A future frame-201 acceptance
must record the software PPU output from the same restored run and compare that
producer-matched image with the PVR front buffer.

Input provenance recorded by the checkpoint bring-up:

- ROM SHA-256: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`.
- BIOS SHA-256: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`.
- Existing host replay evidence reports immediate restore identity and exact
  uninterrupted/restored CPU, IRQ, clock, and device continuation at 64,
  1,024, and 16,384 instructions. This validates checkpoint continuation, not
  frame-201 pixels or PVR output.

## Source provenance

The mGBADC source was inspected and hashed in place; its worktree was not
modified. HEAD is `643aebae1907198c125ab5c9ca672254c60c6b37`; these files are
modified in that worktree, so the hashes below identify the actual bytes
inspected, not a clean HEAD build.

| Source | SHA-256 |
|---|---|
| mGBADC `dc-pvr-gba.c` | `b2c352201ea4b401ced8c5b30c10da985532546c0648b2d3d724903e07a98271` |
| mGBADC `dc-pvr-gba.h` | `8dc4c30f480cb7a9aeb9a2a31d2c748b62c2e7483164087817d7d873145b6f90` |
| mGBADC `dc-pvr-shared-tex.h` | `8de3cbbaa8f8aa8cb05284e2a7baf9c6aa049014a7c72b59da853a7dcfd104a8` |
| mGBADC `src/gba/renderers/video-software.c` | `382a2ad25d144ab67cc9ec19c498a53dec230df2798833dcdb258941a7046463` |
| GBARecomp HEAD | `5e89943528aee7bc1c399bdbd035364a4b41dd10` |
| GBARecomp `gba_ppu.h` | `638e35559f4de833e790bcc3944a15d3a6198ca211a093e6f02909bdcdfb11dc` |
| GBARecomp `gba_ppu.cpp` | `d35bc07858f23d1ad280aab81b7f6ec44c8e15cec31311b4035575a3ba0786b3` |
| GBARecomp `gba_bus.h` | `a94e9e895da8ceddf9b6402e7188a175e346105e7b475ce0260ebffdef065ce4` |
| GBARecomp `gba_bus.cpp` | `84a6f9e6cb069cd3ae718319378ef717fa2ef3faf562d732981c4ec11248cc8d` |
| GBARecomp `runtime_bus_bridge.cpp` | `902ab62e26c3013ea2437d5f095e31d97959e63e9689936e3d3b23be46a602bb` |
| SMA2 `sma2_checkpoint.cpp` | `b87fda1e744a6afaa17923a55fca0a055faf03eb149fcbd2a994feea18c766d2` |
| SMA2 `dreamcast_host_window.inc` | `fdeda53375beacba0bb103a4bc4dd1060ba49ff00d7f8c958bec6e04faf57ec1` |
| SMA2 `CMakeLists.txt` | `b4975026238b4b61dcf6766b75c66525d26eff94cfb7a629adc2f76f4cf107fc` |

GBARecomp's PPU files are dirty in its worktree. Those bytes include the
`PresentationObserver` interface; that existing concurrent work was read only.
The observer API is not snapshot state and is not serialized.

## State/timing and adapter boundary

- SMA2's bridge calls `GbaPpu::render_scanline(y, dispcnt, io, vram, oam,
  palette)` at visible HBlank. The arrays are authoritative live `GbaBus`
  memory. HBlank-timed DMA runs **after** that scanline; VBlank start latches
the completed software framebuffer before VBlank DMA. See GBARecomp
  `runtime_bus_bridge.cpp:573-600`.
- `GbaBus::write8/16/32` is the ordinary CPU/DMA memory path. Its optional
  `BusWriteObserver` intercepts before the store and can suppress it. Snapshot
  restore bulk-deserializes device state and bypasses ordinary bus writes.
  Any adapter using the observer must chain an existing observer and must do a
  full mirror/cache refresh after restore.
- `GbaPpu::PresentationObserver::scanline` receives live state before the
  software PPU's affine accumulator rewrites the IO image used for rendering
  (`gba_ppu.cpp:2518-2559`). The native renderer must receive equivalent
  effective affine state or route those lines through exact software fallback.
  The observer also fires while native rasterization is disabled, so an
  adapter must honor that policy.
- mGBADC `PVRGBAInit()` installs a callback for mGBA's
  `GBAVideoSoftwareRenderer`; `captureLine()` reads that renderer's normalized
  BG, OBJ, palette, OAM, blend, and window state. The callback is invoked by
  mGBA's `drawScanline` (`video-software.c` around its line hook), not by an
  API accepting GBARecomp's raw arrays. `PVRGBAFrame()` later consumes its
  private 160-line capture arrays and mGBA output buffer.
- Therefore a valid bridge needs a persistent mGBA core/renderer object and a
  defined way to mirror palette/VRAM/OAM/IO changes and invoke the line hook at
  each SMA2 raster boundary. Rebuilding only final frame VRAM/registers loses
  per-line writes. Reusing mGBA's software output as fallback would also be
  wrong unless SMA2's same-run software output is explicitly supplied as the
  renderer's fallback source.
- Checkpoint `apply_checkpoint()` restores DEV and timing state but has no call
  to `PresentationObserver::restored()`. The renderer mirror, line history,
  palette shadow, VRAM dirty maps, OAM-clean cache, and PVR resources need an
  explicit invalidation/reseed operation after both current and legacy restore
  paths. Renderer state must remain alive as long as PVR textures and callbacks
  can reference it; mGBADC exposes no renderer shutdown path.
- SMA2's current host window presents completed software frames through the
  centered `vram_s` CPU framebuffer path (`dreamcast_host_window.inc:346+`). A
  native PVR route needs exclusive ownership of presentation for that frame;
  it must not submit a PVR scene and then overwrite the display through the CPU
  presenter. The production route must remain untouched while the diagnostic
  option is off.

This bridge crosses runtime ownership, mGBA internal renderer state, snapshot
restore, fallback surfaces, and scanout selection. The current observer seam
is useful for a separately scoped adapter design, but it does not make the
remaining work a thin pointer adapter.

## Renderer eligibility and unsupported effects

The inspected mGBADC implementation marks a line for fallback when an
eligible visible target-2 OBJ blend is present or the display is in bitmap mode
without an enabled native bitmap path (`dc-pvr-gba.c:501-568`). That is not a
complete SMA2 semantic eligibility predicate. The existing mGBADC fallback
inventory documents incomplete windows/OBJ-window, mosaic, temporal
palette/OAM, broad priority/tie cases, sprite clipping/sampling, and blending
coverage. A zero fallback count alone is not a correctness result.

The selected checkpoint has no producer-matched per-line DISPCNT, window,
blend, mosaic, affine, or palette/OAM history. Thus **no specific frame-201
scanline can yet be classified as native-eligible or unsupported**. Reporting
specific fallback row numbers before such a trace would be invented. A future
opt-in run must emit per-line consumed state and explicit reason codes, and any
unsupported row must use the same-run GBARecomp software pixels as fallback.

## Host validation performed / not established

Performed by source and evidence review only; this task ran no build, guest
replay, or test. Existing host evidence establishes:

- the checkpoint can be restored and continued with matched serialized guest
  state at the cited instruction endpoints;
- the synthetic BG0 adapter/software oracle match and its hardware front-buffer
  gate pass for that fixture;
- the native mGBADC renderer has its own bounded tests and documented limits.

Not established:

- frame-201 same-run software reference or per-line graphics census;
- a GBARecomp-to-mGBA live state adapter;
- native eligibility/fallback for any SMA2 gameplay scanline;
- counter/lifecycle behavior in a gameplay harness;
- gameplay PVR pixel correctness, display ownership integration, or performance.

The previous counter-ordering regression is covered in the synthetic fixture
harness: it calls `PVRGBAFrame()` before validating the published counters. The
successful hardware log for that test is under
`docs/results/evidence/sma2_native_pvr_bg0_dc_hw_20261018/counter_order_fix/`.

## Smallest justified next milestone

Before enabling PVR in the game executable, separately scope a default-OFF
**host-only gameplay observer prototype** that restores the frame-200
checkpoint, records frame-201 scanline state at the existing observer seam,
and proves a producer-matched software reference and deterministic state
fingerprint without attaching mGBA. It should report consumed display/effect
registers, write/latch generations, and exact scanline boundaries. If that
trace demonstrates a scene dominated by semantics covered by the native
renderer, the next separate implementation should be an isolated Dreamcast
shadow-mGBA adapter that:

1. mirrors bus writes and performs explicit post-restore full reseed;
2. feeds each captured scanline into mGBA's renderer hook at the same HBlank
   boundary, including effective affine state or explicit software fallback;
3. supplies GBARecomp's same-run completed RGB888 frame as the fallback source;
4. owns a dedicated native PVR presentation route, leaving the production
   renderer and CPU presenter unchanged when disabled.

Do not start that adapter until its observer ABI, mGBA renderer lifecycle, and
fallback ownership are scoped and source-pinned.

## Bounded hardware acceptance procedure (future authorization required)

1. Freeze the exact checkpoint-restore build, ROM, BIOS, checkpoint, native
   renderer source, and full ELF hashes. Generate a same-run RGB888 reference
   for the identified restored GBA frame and record its frame/state marker.
2. Build in a separate scratch target with the Dreamcast default-off option
   explicitly enabled. In the same shell source
   `/opt/toolchains/dc/kos/environ.sh`, set the documented relocated GCC 17
   `KOS_CC_BASE`/compiler tools, and verify `command -v sh-elf-gcc` resolves to
   `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc` (GCC 17.0.0, verified in the same shell).
3. Use a network-enabled shell with UDP support. Immediately before the single
   launch, verify ELF, ROM, BIOS, checkpoint, and renderer source hashes. Check
   that `/tmp/mgbadc-dreamcast-console.lock` and `kos-tool` are absent, then
   acquire the established `kos-tool-locked.sh` lock. Do not retry after a
   failed launch.
4. Restore the checkpoint and log restore identity. Mark the first complete
   resumed GBA frame (expected frame 201); do not align by wall-clock delay or
   Dreamcast VBlank alone. Collect per-line source/effect/fallback counters.
5. Call `PVRGBAFrame()` before reading its published counters. Require all 160
   scanlines accounted for, explicit supported/unsupported classification,
   correct fallback row ownership, successful render completion, and stable
   displayed front/back page addresses around capture.
6. Capture the actual displayed 240×160 RGB565 front buffer through the
   preserved SMA2 capture method. Compare all 38,400 pixels with the
   producer-matched same-run software frame converted by the stated RGB565
   rule. Record mismatch count and first coordinates, complete serial output,
   framebuffer SHA-256, all source/input/ELF hashes, waits, page identities,
   fallback rows, and hardware exit status. Stop on any failed gate.

## Performance readiness (no optimization performed)

After correctness passes, compare matched native-OFF and native-ON runs from
identical checkpoint/state/input windows and compiler settings. Record:

- software PPU scanline composition time and calls (`g_prof_scanline_ns` /
  count) for OFF and remaining software fallback;
- adapter synchronization cost, native layer/sprite preparation, upload, and
  TA submission CPU time separately;
- SH-4 cycle counts and total emulated-frame time;
- I-cache and D-cache behavior using the existing validated Dreamcast PMU
  instrumentation, with exact event/configuration and perturbation notes;
- native fallback count/rows and PVR completion timing;
- matched ELF, source, ROM, BIOS, checkpoint, and input hashes.

A correct frame or lower upload cost is not a performance win. The relevant
result is reduced total SH-4 work from removing software BG/OBJ pixel
composition without shifting equal or greater cost into synchronization,
layer construction, fallback, or PVR stalls.

---

## Implementation update — 2026-10-19

### Verdict

**READY FOR HARDWARE — the persistent bridge, host state-contract checks, exact
frame-201 checkpoint replay, and both Dreamcast configurations are built and
host-validated.** The source-level temporal memory concern was checked against
the selected frame's write trace and no visible PAL/VRAM/OAM writes occurred.
This verdict authorizes a hardware acceptance attempt only as the next separately
authorized step; it does not claim native pixel correctness or scanout
equivalence. No hardware launch was performed.

### Exact implementation boundaries

SMA2 changes touched `CMakeLists.txt`, `tools/build_dreamcast.sh`,
`src/dreamcast_host_window.inc`, `src/snapshots/sma2_checkpoint.cpp`,
`src/dreamcast_native_pvr_bridge.{h,cpp}`,
`src/dreamcast_native_pvr_bridge_hook.{h,c}`,
`src/dreamcast_native_pvr_bridge_contract.h`,
`tools/test_native_pvr_bridge_contract.cpp`, and the opt-in host diagnostic
`tools/sma2_snapshot_diff.cpp`. CMake adds the separate default-OFF contract
test target. The existing default presentation path remains off:
`SMA2_NATIVE_PVR_GBA_RENDER` defaults OFF, and
`SMA2_NATIVE_PVR_SKIP_SOFTWARE` also defaults OFF and requires the native option.
The synthetic BG0 fixture was not changed.

One additive GBARecomp seam was necessary and explicitly within the authorized
cross-runtime bridge scope: `PresentationObserver::native_scanline()` defaults
to false and is called after effective affine references are computed but
before software composition. The bridge chains the pre-existing scanline,
frame, restore, and bus-write observers. The observer's true result skips only
that row's software composition. No GBARecomp CPU, PPU renderer, or mGBADC
renderer implementation was replaced. The existing `gba_ppu.cpp` affine
accumulator behavior is consumed as-is.

### Shadow renderer and lifecycle

The bridge creates one mGBA GBA core and software renderer at Dreamcast window
open, gives it a 240-pixel fallback buffer, and initializes the existing
`PVRGBAInit()` path. It loads a dummy ROM only to initialize the core/device
renderer. The shadow CPU is never stepped (`shadow_cpu_steps=0`); it does not
advance GBA time. The core/renderer is intentionally process-lifetime because
mGBADC retains PVR textures/callbacks and exposes no safe shutdown path.

At each authoritative GBA scanline boundary, the observer synchronizes relevant
display registers (including effective affine X/Y), then calls the existing
mGBADC draw-scanline hook on the persistent shadow renderer. CPU/DMA palette,
VRAM, and OAM writes are chained to existing bus observers and mirrored using
the passed new value; framebuffer/scanline timing remains owned by GBARecomp.
HBlank DMA writes after the current line are seen at subsequent lines. The
first full synchronization copies 1,024 palette bytes, 96 KiB VRAM, and 1 KiB
OAM. The measured instrumentation counts bytes/writes, but no performance claim
is made.

On both legacy and current successful checkpoint restore paths, SMA2 now calls
the observer's `restored()` notification after commit. The bridge resets the
mGBA software renderer cache, resets mGBADC line capture bookkeeping, reseeds
all three memory regions, clears line/fallback history, and resets per-frame
capture ownership. Failed restores do not notify. The same reseed operation is
safe to repeat.

### Native eligibility, fallback, and presentation

The default-OFF bridge has two opt-in modes:

- **Validation** (`SMA2_NATIVE_PVR_GBA_RENDER=ON`,
  `SMA2_NATIVE_PVR_SKIP_SOFTWARE=OFF`): software composition runs for all rows
  as the oracle while the native backend captures eligible rows.
- **Native-skip** (both ON): GBARecomp skips software pixel composition only
  where the existing native renderer reports a captured row and the local
  eligibility checks pass. Unsupported rows stay software-rendered.

Explicit local fallback reasons currently include windows/OBJ-window,
bitmap modes above mode 2, blending/brightness, BG mosaic, and OBJ mosaic; a
backend decline is recorded separately. Fallback rows are supplied from the
same frame's latched GBARecomp software RGB888 output. This is same-frame row
ownership by construction, but has not been checked against actual PVR output.
The simple eligibility classifier is not proof that every mGBADC
priority/affine/sprite-clipping behavior matches SMA2. The source-pinned
backend documents that mid-frame palette/OAM changes are unsupported and
`PVRGBAFrame()` builds resources from the shadow's current state. A bounded
same-run frame-201 host census found zero visible PAL, VRAM, or OAM writes, so
that temporal resource limitation is not triggered in this selected frame.
Future frames need the same trace gate or conservative fallback if resource
writes occur between visible scanlines.

After all 160 GBA rows have completed, frame-ready populates mGBADC's fallback
surface from the latched same-frame software buffer, calls `PVRGBAFrame()`,
then logs the public captured/displayed/fallback counters. Native PVR owns
presentation when initialized; the regular CPU `vram_s` presenter is suppressed
for that frame. When the option is OFF, the existing CPU presentation path is
unchanged. The later frame-201 hardware acceptance confirmed the counter order,
front-buffer capture, page ownership, and exact pixels for this selected scene.

### Checkpoint and same-run software oracle

Selected checkpoint remains
`docs/results/evidence/sma2_pc_gameplay_checkpoint_20261016/pc_frame200_checkpoint.gbas`.
ROM, BIOS, and checkpoint SHA-256 values are respectively:

- ROM: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`
- BIOS: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`
- Checkpoint: `cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188`

A dedicated 32-bit host `sma2_snapshot_diff` accepted the exact checkpoint
(profile 4), restored it, advanced one completed frame, and captured the
latched software output and runtime fingerprint. Evidence log:
`docs/results/evidence/sma2_gameplay_native_pvr_bridge_20261019/frame201_host_oracle.log`.
Frame 201 fingerprints:

- RGB888 FNV-1a: `e6b64e95140b714f`
- RGB565 little-endian FNV-1a: `0d585ef913d00d74`
- Device state: `c6bd6262af7a0083`; CPU: `70178d57e530541a`
- IRQ: `97049efb37419ae3`; clock: `a59eaefddbb30930`

This is a trustworthy host software oracle from that restored run. The
hardware validation independently logged the same software hashes and PPU
fingerprint on the Dreamcast, then compared the actual captured PVR frame
against that same-run software output.

### Host validation and build provenance

- GBARecomp `ppu_smoke_tests`: PASS, including native-scanline observer,
  chaining, restore notification, and line skip coverage.
- `sma2_rgb565_sidecar_present_tests`: PASS (38,400 active pixels, 256×256
  staging, centered CPU copy). This is an unchanged unrelated presentation
  regression and is not evidence for native BG/OBJ output.
- `sma2_snapshot_diff` host 64-bit: PASS, 45 checks, 0 failures, including
  repeated restore and refusal gates.
- Frame-200 checkpoint restore and one-frame run: PASS in the dedicated
  profile-4 32-bit host executable under `qemu-i386`; direct i386 execution is
  blocked by the host seccomp policy (SIGSYS), so QEMU supplied only the
  required host ABI execution.
- Frame-201 resource-write census: PASS for the selected checkpoint. Across
  all 160 visible rows the host run recorded 0 PAL writes, 0 VRAM writes, and
  0 OAM writes. This supports frame-201 texture/OAM/palette lifetime readiness
  but does not replace the actual PVR output comparison.
- Dreamcast validation build: PASS with native PVR enabled, software oracle
  retained, checkpoint and autoload enabled. ELF SHA-256:
  `4eb285869cd8de2cf1b452497042613f28639566a95f2742cfdbc4ece93481a5`.
- Dreamcast native-skip build: PASS with selective software composition skip
  enabled. ELF SHA-256:
  `feb583d80473100c46cc49e90190b33fee784537b7885ba07b0609aa9e641f53`.
- New SMA2 bridge contract tests: PASS for exact palette/VRAM/OAM address
  mapping, width/range rejection, all 38 mirrored display-register offsets,
  DISPCNT-last ordering, and per-effect fallback classification. The production
  bridge calls the same pure helper functions tested here.
- The implementation-stage builds were performed by `tools/build_dreamcast.sh`, which sourced KOS
  `environ.sh` and verified the installed GCC 17 path from
  `command -v sh-elf-gcc`. No hardware launch occurred at that stage; see the
  hardware acceptance results below.
- mGBADC original dirty checkout was not modified. Its source-pinned copy is
  `/tmp/sma2-bg0-mgbadc-source`; original checkout source hash remains
  `b2c352201ea4b401ced8c5b30c10da985532546c0648b2d3d724903e07a98271`, while
  the isolated integration copy `dc-pvr-gba.c` hash is
  `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`.
  The copy only adds a forced per-line fallback/reset hook and C++ linkage
  guards in the header; renderer composition code is otherwise source-pinned.

Build hashes and the diagnostic ELF are under
`docs/results/evidence/sma2_gameplay_native_pvr_bridge_20261019/`. The ELF is
for a controlled diagnostic run, not evidence of physical hardware output.

### Hardware acceptance update — 2026-10-19

**PASS for the selected restored frame-201 scene.** The separately authorized
two-stage Dreamcast run first forced all rows through software fallback, then
ran the effective-state classifier with native BG/OBJ enabled. Both restored
frame 200 and captured completed frame 201. The host-side fallback correction
disables RGB565 PVR dithering; the all-fallback run matched the software oracle
exactly, confirming the earlier small positive channel deltas came from output
dithering for this path.

- Force-fallback ELF SHA-256:
  `21ef499c6ca344b0533680fa9b270ef15bae9e76b7e28eaa5c90e2abdd396474`;
  160 fallback rows, 160 captured/displayed, 0/38,400 mismatches.
- Native-eligible ELF SHA-256:
  `ce326fbf7f82e27c743985c90f1e038b27a50fad55f84c2b0aca239b55702103`;
  160 native BG/OBJ rows, zero fallback rows, 160 captured/displayed,
  0/38,400 mismatches.
- Both software and PVR RGB565 files are 76,800 bytes and have SHA-256
  `0ac1575875ad5bb2a4c95e82d949fcbea28f7e8c6e395b565811ff14929bcf6a`.
- Both runs logged software RGB888/RGB565 FNV-1a
  `e6b64e95140b714f` / `0d585ef913d00d74` and PPU state FNV-1a
  `12f36c915005eac5`.
- Render waits returned success, VBlank was observed, and front/back page
  addresses `0xa511f000` / `0xa591f000` remained distinct and stable around
  capture. Both application exit statuses were zero; the shared lock was
  released after each run.

Complete serial logs, preflight hashes, both ELFs, raw captures, checksums, and
the comparison report are under
`docs/results/evidence/sma2_gameplay_native_pvr_bridge_20261019/hardware_followup_20261019/`.

### Consecutive-frame hardware acceptance — 2026-10-19

**PASS for frames 201–204 in the same restored scene.** A single bounded
launch used the diagnostic capture extension to restore frame 200 and capture
four complete software-oracle/native-PVR frame pairs. All four frames had
160/160 native rows, zero fallback rows, 160 captured and displayed lines,
successful render waits, VBlank observation, and stable distinct front/back
page ownership. Each 240×160 RGB565 PVR front-buffer capture had 0/38,400
mismatches against the software output from that exact completed frame; an
independent host `cmp` passed for each 76,800-byte file pair. The state and
framebuffer hashes changed across frames 202–204, so these were subsequent
gameplay frames rather than repeated copies of frame 201.

The acceptance ELF SHA-256 is
`5d79a25a7786da17aee4ef041d5d55db46e12943601ac8b0588d82f3788a1e08`.
It was built with native PVR and the complete software oracle enabled,
checkpoint autoload enabled, and four-frame front capture. Full hashes,
serial output, raw captures, and lock/toolchain evidence are in
`docs/results/evidence/sma2_gameplay_native_pvr_bridge_20261019/multiframe_20261019/`.

All rows in this sequence were native eligible, so mixed native/fallback row
ownership remains untested. Other effect combinations and temporal palette,
OAM, or VRAM writes between visible scanlines also remain unvalidated. The
four-frame result does not establish general PVR renderer correctness or a
performance win. The production rendering default remains unchanged.
