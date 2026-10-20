# SMA2 frame-201 nullDC3 native palette color model — 2026-10-19

## Verdict

**No SMA2 renderer change is indicated by the captured frame.** The earlier 36,824-pixel residual came from applying the RGB565 fallback texture model to the native BG/OBJ palette path. The native path uses 5-bit green in ARGB1555. A source-backed ARGB1555/refsw2 model predicts the captured RGB565 value at **all 38,400 pixel coordinates**. This resolves the emulator pixel discrepancy for the selected frame. The physical Dreamcast comparison remains the separate hardware gate; it previously passed without emulator color adjustment.

## Evidence and model

The original capture and software oracle are in `evidence/sma2_nulldc3_frame201_offline_audit_20261019/pcroot/`. They have exactly 20 distinct colors each. Every software color maps to exactly one captured PVR color, with the same per-color pixel count. The pixel layout is unchanged; no coordinate shift is needed.

The exact source-pinned mGBADC renderer used by the ELF is `/tmp/sma2-bg0-mgbadc-source/src/platform/dreamcast/native/dc-pvr-gba.c` (SHA-256 `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`). Its `updatePalette()` packs GBA RGB555 into ARGB1555 (line 978 onward) and `PVRGBAInit()` selects `PVR_PAL_ARGB1555` (line 4644). The current dirty mGBADC checkout has different bytes and was not used as build provenance.

The persistent nullDC3 source `libswirl/rend/refsw2/TexUtils.h:22` expands each ARGB1555 channel as `c5 << 3`, without bit replication. Its `refsw_lists.cpp:532-548` writes RGB565 with `floor(c8 × channel_max / 255)` when dithering is off. The guest's software oracle conversion is `src/dreamcast_native_pvr_bridge.cpp:135-140`. For this palette-sourced frame, recover the GBA green 5-bit value as `software_g6 >> 1`, then apply the native palette model:

```
expected_r5 = floor((software_r5 << 3) * 31 / 255)
expected_g6 = floor(((software_g6 >> 1) << 3) * 63 / 255)
expected_b5 = floor((software_b5 << 3) * 31 / 255)
```

The original RGB565 fallback model instead expands all six software green bits, so it predicts one green level too high for odd `software_g6` values. Example at (0, 0): software `0x6dbf`; fallback model `0x659e`; native ARGB1555 model and captured PVR both `0x657e`.

| Comparison | Exact mismatches / 38,400 |
|---|---:|
| Raw software RGB565 vs captured PVR | 36,888 |
| Software transformed through RGB565 fallback/refsw2 path | 36,824 |
| Software transformed through native ARGB1555 palette/refsw2 path | **0** |

The exact comparison code and output are `evidence/sma2_nulldc3_frame201_offline_audit_20261019/compare_refsw2.py` and `pixel_comparison.txt`. The same captured files were used for all three comparisons; no launch, rebuild, or emulator setting change was made during this analysis.

## What to fix

Keep the guest renderer and mGBADC renderer unchanged. For future **emulator-only** pixel gates, select the expected color transform by row/source ownership: RGB565 texture model for software fallback rows, ARGB1555 palette model for native BG/OBJ rows. A mixed-row gate must apply the correct model to each row and must not infer native ownership from colors. Preserve raw captures and raw mismatch counts alongside model-adjusted results. The physical Dreamcast gate must compare raw RGB565 pixels exactly and must never apply either nullDC3 model.

The reusable comparator now implements this rule in `tools/compare_sma2_nulldc3_pvr.py`. It requires the exact `SMA2_NATIVE_PVR_LINE_CLASS` record for the selected frame, all 160 distinct row entries, passing capture counters, stable page ownership, and RGB565 file hashes matching the serial capture record. Missing or inconsistent metadata produces `RESULT=ERROR` with a nonzero exit status; it never guesses a row's color path.

Frame-201 invocation:

```sh
python3 tools/compare_sma2_nulldc3_pvr.py \
  --serial docs/results/evidence/sma2_nulldc3_frame201_offline_audit_20261019/serial.log \
  --frame 201 \
  --software docs/results/evidence/sma2_nulldc3_frame201_offline_audit_20261019/pcroot/sma2_gameplay_frame201_software.rgb565 \
  --pvr docs/results/evidence/sma2_nulldc3_frame201_offline_audit_20261019/pcroot/sma2_gameplay_frame201_pvr.rgb565
```

Result: 160 native rows, zero fallback, 36,888 raw mismatches, **zero ownership-model mismatches**, `RESULT=PASS`. This closes the emulator **pixel-equivalence** gate for frame 201 under the documented native palette model. It does not close full guest-state equivalence.

As a missing-metadata check, the same command with the older frame-201 run's serial log exits 2 before reading the capture files: `expected exactly one SMA2_NATIVE_PVR_LINE_CLASS ... found 0`. No emulator launch was involved.

This model is established for this native-eligible frame. It does not establish every native effect path or full guest-state equivalence: the ELF still lacks the independent host's CPU/device/IRQ/clock fingerprints. The available frame, PC, software framebuffer, PPU, and input evidence supports the intended frame association.

## Investigation closure

The frame-201 **renderer pixel-equivalence investigation is closed** for the
selected scene: 160 native rows executed and the source-backed nullDC3 native
color model matches every captured pixel. The mixed-row nullDC3 gate is also
complete: the recorded 8 fallback and 152 native rows match the corresponding
ownership-selected models at 0/38,400. The earlier proposal to run the mixed
fixture was completed and is retained in
[`SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md`](SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md).

Preserve `tools/compare_sma2_nulldc3_pvr.py` and the raw captures as the
baseline for later emulator runs. Full guest CPU/device/IRQ/clock fingerprint
equivalence remains incomplete and separate from pixel equivalence. Physical
Dreamcast raw-capture results remain authoritative for hardware correctness.
The next renderer milestone is native validation on an existing native-eligible
gameplay checkpoint, starting with `g_599`; see
[`SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md`](SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md).
