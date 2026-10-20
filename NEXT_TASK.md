# Current SMA2 renderer handoff — frame-201 pixel gates CLOSED

The nullDC3 frame-201 all-native and mixed-row pixel-comparison gates are
CLOSED. Physical Dreamcast acceptance remains authoritative: native frame 201
passed at 160 native / 0 fallback rows and 0/38,400 raw RGB565 mismatches;
mixed frame 201 passed at 152 native / 8 fallback rows and 0/38,400 raw
mismatches. nullDC3 passed 0/38,400 only after the documented ownership-based
emulator color model. Do not apply its RGB565 fallback model to native
ARGB1555 palette rows. Full CPU/device/IRQ/clock fingerprint equivalence is
still incomplete. Details and reusable offline validation:
[`SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md`](docs/results/SMA2_NULLDC3_RENDERER_PIXEL_EQUIVALENCE_CLOSURE_20261019.md),
[`SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md`](docs/results/SMA2_NULLDC3_FRAME201_NATIVE_COLOR_MODEL_20261019.md),
[`SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md`](docs/results/SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md).

## Next task: validate native PVR on grid checkpoint g_599

Use the existing default-OFF SMA2 native-PVR diagnostic path and
`docs/results/evidence/sma2_iwram_aot_invalidation_20261019/normal_gameplay_benchmark/music_scenes/grid_ckpt/g_599.gbas`
(SHA-256 `146dc1b989598c5505d46c1cf84377e52b6f5b3e0b94a8db53d4ec58e6af9a77`).
The host probe reports 160/160 eligible rows, but this is only an upper bound.
First verify checkpoint/input hashes and restore behavior, determine the first
completed guest-frame number from runtime evidence, and establish a same-run
software oracle. Keep software composition enabled and IWRAM AOT/native-skip
disabled for this correctness gate. Do not reuse a saved PNG or unrelated
frame as the oracle.

Request a separately authorized, single bounded Dreamcast run only after
offline preflight succeeds. Acceptance: native rows actually execute; all 160
rows have explicit native/fallback ownership and reasons; guest state matches
the same-run reference at the selected boundaries; PVR completion and stable
front/back ownership pass; and the displayed 240x160 RGB565 buffer matches the
same-frame software oracle at exactly 0/38,400 raw pixels. Stop on the first
restore, state, lifecycle, ownership, or pixel failure. Keep renderer defaults
unchanged. Consider `g_999` only after this checkpoint passes. Preserve the
full guest CPU/device/IRQ/clock fingerprint gap as a separate limitation and
make no performance claim from correctness runs. Eligibility evidence:
[`SMA2_NATIVE_ELIGIBILITY_AND_CAPTURE_ORDER_20261019.md`](docs/results/SMA2_NATIVE_ELIGIBILITY_AND_CAPTURE_ORDER_20261019.md).

---
