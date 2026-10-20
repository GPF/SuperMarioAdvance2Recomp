# SMA2 nullDC3 renderer pixel-equivalence closure — 2026-10-19

## Verdict

**CLOSED for frame-201 pixel equivalence under nullDC3's source-backed color models.** The reusable comparator passes both all-native and mixed-row captures. Physical Dreamcast captures remain the authority for hardware pixel correctness. Full CPU/device/IRQ/clock fingerprint equivalence is incomplete and is not implied by these pixel results.

## Accepted gates

| Gate | Renderer ownership | Pixel result | Evidence |
|---|---|---|---|
| Physical Dreamcast frame 201, native | 160 native, 0 fallback | 0/38,400 raw RGB565 mismatches | [`SMA2_GAMEPLAY_NATIVE_PVR_INTEGRATION_20261018.md`](SMA2_GAMEPLAY_NATIVE_PVR_INTEGRATION_20261018.md), hardware follow-up evidence under `evidence/sma2_gameplay_native_pvr_bridge_20261019/hardware_followup_20261019/native_eligible/` |
| Physical Dreamcast frame 201, mixed | 152 native, 8 fallback (rows 0–7) | 0/38,400 raw RGB565 mismatches | [`sma2_native_pvr_mixed_rows_20261019/README.md`](evidence/sma2_native_pvr_mixed_rows_20261019/README.md) |
| nullDC3 frame 201, native | 160 native, 0 fallback | 0/38,400 ownership-modeled mismatches | [`SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md`](SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md) |
| nullDC3 frame 201, mixed | 152 native, 8 fallback | 0/38,400 ownership-modeled mismatches | [`SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md`](SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md) |

The nullDC3 native renderer path emits ARGB1555 palette colors. Do not apply the RGB565 fallback conversion model to native-owned rows. The comparator uses the RGB565 texture model for fallback rows and the ARGB1555 palette model for native rows, selected only from the recorded per-row ownership metadata. These are emulator-only transformations. Raw mismatch counts remain visible in its output and are not acceptance values for physical hardware. The source-backed explanation is in [`SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md`](SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md).

## Reusable comparator and fail-closed checks

[`tools/compare_sma2_nulldc3_pvr.py`](../../tools/compare_sma2_nulldc3_pvr.py) requires exactly one ownership record covering all 160 rows, consistent native/fallback counters, complete capture/display and fallback counts, successful render-ready/render-done/VBlank/stability fields, distinct unchanged front/back page identities, complete pixel buffers, and capture-file FNV hashes matching the serial record. Missing or inconsistent metadata is an error; the tool does not infer ownership from colors.

Offline replay against retained evidence:

- All-native: 160 native / 0 fallback; raw 36,888/38,400; modeled 0/38,400; `RESULT=PASS`.
- Mixed: 152 native / 8 fallback; raw 36,888/38,400; modeled 0/38,400; `RESULT=PASS`.
- Missing ownership: exits 2 with `RESULT=ERROR` before reading pixel buffers.

The evidence logs and raw captures remain in `evidence/sma2_nulldc3_frame201_offline_audit_20261019/` and `evidence/sma2_nulldc3_mixed_rows_20261019/retry120/`. The raw comparisons are intentionally nonzero under nullDC3's conversion path; the per-row modeled comparisons are the emulator-only acceptance gate. The reproducible command outcomes and fail-closed mutations are summarized in [`evidence/sma2_nulldc3_renderer_closure_20261019/offline_comparator_regression.txt`](evidence/sma2_nulldc3_renderer_closure_20261019/offline_comparator_regression.txt).

## Next renderer task

Validate the native renderer on the existing native-eligible `g_599` gameplay checkpoint, then consider `g_999` only after `g_599` passes. The host eligibility probe reports 160/160 eligible rows for both; this is only an eligibility upper bound, not evidence of native execution or correct output. See [`SMA2_NATIVE_ELIGIBILITY_AND_CAPTURE_ORDER_20261019.md`](SMA2_NATIVE_ELIGIBILITY_AND_CAPTURE_ORDER_20261019.md).

Before requesting hardware time, verify the exact checkpoint and deterministic input provenance, restore it through the production restore path, establish the first completed-frame number from runtime records, and produce a same-run software oracle. Then prepare the existing default-OFF diagnostic route with software composition enabled. Acceptance on Dreamcast requires demonstrated native rows, explicit fallback reasons, per-frame guest-state fingerprints, all 160 rows accounted for, successful `PVRGBAFrame()` completion and stable displayed-page ownership, and exact raw RGB565 comparison of all 38,400 pixels against the same-frame software oracle. Preserve physical hardware raw comparisons without emulator color correction. Stop on the first restore, state, lifecycle, ownership, or pixel mismatch. Keep production defaults unchanged.

Do not repeat the frame-201 pixel investigation without new contradictory evidence. Do not call pixel equivalence full guest-state equivalence. Do not infer a performance gain from these correctness runs.
