# SMA2 mixed-row nullDC3 comparator gate — 2026-10-19

## Verdict: PASS — ownership-selected emulator model matches all pixels

The 120-VBlank run restored checkpoint frame 200 and completed frame 201. The SMA2 mixed-row fixture reported 8 software-fallback rows (0–7) and 152 native BG/OBJ rows (8–159). The reusable nullDC3 comparator selected the RGB565 fallback model for rows 0–7 and the ARGB1555 palette model for rows 8–159, then matched the captured PVR output at **0/38,400 pixels**. This verifies mixed-row model selection in one frame.

The first attempt, with a 24-VBlank bound, stopped before checkpoint restore and is preserved separately in `evidence/sma2_nulldc3_mixed_rows_20261019/attempt_result.md`. The completed run is `evidence/sma2_nulldc3_mixed_rows_20261019/retry120/`. No physical Dreamcast launch occurred in this task.

## Preflight provenance

The 19,148,148-byte original fixture ELF was copied and stripped for nullDC3. The stripped ELF is 2,161,936 bytes with SHA-256 `bb0e4979171ef4deb5e90a04cf70bfa60f71fa4221799d5a97ef2c9f33beb98c`. Original ELF SHA-256 is `cf5fbacbb005e49366a91cf9961b9c6990f5a971d121539580cb911b3f5c7ac7`. Original and stripped loadable-image hashes match: `5fdd1215c40725c2313ec34089f089da38255cad7f1c4979849ac0e8bf6f675b`.

| Input | SHA-256 |
|---|---|
| nullDC3 executable | `f071573357a70b60d2e6ba6120fe83c475e45833c2a8970a8256382cab978b75` |
| ROM | `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3` |
| BIOS | `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570` |
| Frame-200 checkpoint | `cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188` |
| game.toml | `ecbb5dd7835e709570362dae8d23caf61291af61a1d5e1283f27bf19a17d1f2a` |
| Pinned mGBADC renderer | `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef` |

The run uses the already-existing PVR validation ELF, with native BG/OBJ enabled, software composition retained, mixed-row fixture enabled, native-skip disabled, and checkpoint autoload enabled. The ELF's source/build/input provenance is in `evidence/sma2_native_pvr_mixed_rows_20261019/README.md`; the launch-specific hash manifest and exact command are in the `retry120/` directory.

## Frame-201 evidence

- Serial confirms `CHECKPOINT_OK restored frame=200`.
- Frame and PC: guest frame 201, PC `0x0800052e`; neutral input `keyinput=0x03ff`.
- Guest PPU fingerprint `12f36c915005eac5`; software RGB888/RGB565 FNV hashes match the established frame-201 oracle: `e6b64e95140b714f` / `0d585ef913d00d74`.
- Ownership counters: 160 rows accounted, 152 native, 8 fallback, 160 captured and displayed; fixture and counter checks pass.
- Page ownership is stable across capture (`front=0xa511f000`, `back=0xa591f000`); nullDC3 PVR ready/render wait return values are zero, indicating successful waits in this harness.
- Raw guest comparison reports 36,888 mismatches because it compares unmodeled software RGB565 directly against refsw2 output. This raw diagnostic remains visible and is not the modeled acceptance result.
- The reusable comparator validates capture-file FNV hashes against serial, reports the raw count separately, and applies ownership-selected conversion. Result: `ownership_model_mismatches=0/38400`, `RESULT=PASS`.

Exact output hashes:

- Software oracle SHA-256: `0ac1575875ad5bb2a4c95e82d949fcbea28f7e8c6e395b565811ff14929bcf6a`.
- PVR capture SHA-256: `7b27467f29a5f29d36f21c76fda497dadda5ff534e81fabea4109232d8fc1fa7`.

The guest later emitted `SMA2_NATIVE_PVR_ACCEPTANCE frame=201 result=FAIL` because its built-in acceptance path performs raw equality without nullDC3's model. The process then reached the configured 120-VBlank bound and exited 0. This emulator log result does not override the offline model-aware comparator result; both are recorded so the distinction stays explicit.

## Limits

This result validates the ownership-to-color-model selection in the nullDC3 emulator for this mixed fixture and frame. It does not establish full CPU/device/IRQ/clock fingerprint equivalence because the ELF does not emit those independent host fields. It does not change the already-passing physical Dreamcast mixed-row evidence, and it is not a performance result.

## Investigation closure

The nullDC3 comparator investigation is closed. Established results:

1. All-native frame 201 matches the source-backed native ARGB1555/refsw2 color model.
2. Mixed frame 201 matches the fallback RGB565 and native ARGB1555 models selected from all 160 ownership records.
3. Raw comparisons remain reported independently; a guest raw `ACCEPTANCE=FAIL` caused by the unmodeled emulator conversion remains visible alongside model-adjusted acceptance.

The 24-VBlank attempt ended before checkpoint restore and is an insufficient execution window, not a renderer failure. The missing full CPU/device/IRQ/clock fingerprints remain a separate validation gap. Reuse `tools/compare_sma2_nulldc3_pvr.py` when future renderer work needs emulator-side pixel evidence; further comparator refinement is not planned. Continue with native coverage or performance work.
