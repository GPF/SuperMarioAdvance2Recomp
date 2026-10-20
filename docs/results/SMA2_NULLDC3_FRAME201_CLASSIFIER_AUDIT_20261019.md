# SMA2 nullDC3 frame-201 classifier audit — 2026-10-19

## Verdict

**Offline audit complete; no emulator launch performed.** The evidence favors a build/classifier mismatch as the cause of the earlier `0x15` fallback. It does not prove that the guest PPU state was identical in all respects. The capture-capable `native_eligible` ELF is already demonstrated to restore frame 200, capture resumed frame 201, execute all 160 native rows, and pass its same-run Dreamcast capture comparison; it is suitable for the proposed emulator gate, subject to the guest-state caveat below.

## ELF comparison

| Artifact | SHA-256 | Relevant evidence |
|---|---|---|
| Launched older validation ELF | `4eb285869cd8de2cf1b452497042613f28639566a95f2742cfdbc4ece93481a5` | Run 2; PVR enabled, frame-200 checkpoint autoload, software oracle; 19,095,372 bytes. No capture diagnostic strings or raw RGB565 capture. |
| Existing capture-capable `native_eligible` ELF | `ce326fbf7f82e27c743985c90f1e038b27a50fad55f84c2b0aca239b55702103` | Built with native PVR and capture enabled, checkpoint autoload, software composition retained, native-skip and force-fallback off. Prior Dreamcast evidence exercises frame 201. |
| Prepared strip-debug copy | `dfc92bbd4b89c0f3e7af15da8839a60efd42e733b59094713ce7808a3d30cfa9` | 2,159,628 bytes, below nullDC3's 16 MiB loader limit. |

The older and native-eligible ELFs have different loadable-image hashes (`cbca0fc7…b552` versus `784fc9a8…1feebd`). Stripping the native-eligible copy preserves its loadable image exactly (`784fc9a8…1feebd`). Therefore they are distinct builds, and the new file is a strip-only derivative of the capture-capable build, not a substitution of debug-stripped old code. Full hashes, staged inputs, and image comparison are in `evidence/sma2_nulldc3_frame201_offline_audit_20261019/`.

The earlier ELF's original source tree/build manifest is not embedded in the retained run evidence, so an exact source-revision or compile-flag diff against the later ELF cannot be reconstructed. What is established: both were PVR/interpreter validation artifacts with checkpoint restore and software oracle; the first lacks the capture path. The later build's recorded bridge hash is `61e11f5c316887802c3afd61dd0bb280656433b15a8442437913012b9812fb54`; its build record identifies KOS GCC 17.0.0. Treat the precise source delta as unverified.

## Why the probe and runtime disagreed

The host probe is not a separate classifier: its scratch observer includes the bridge contract and calls `classify_fallback()` on each scanline. It is host-only and reports eligibility, not backend execution. Its frame-200 trace has DISPCNT `0x3300` and 160/160 rows with no window, blend, or mosaic fallback (`evidence/sma2_native_eligibility_probe_20261019/raw/pc_frame200.out`).

In the current contract (`src/dreamcast_native_pvr_bridge_contract.h:95-132`), WIN0 can be accepted when it covers the entire 240×160 display, permits BG/OBJ layers, and masks effects. The classifier therefore does not treat the mere WIN0 enable bit as an unsupported window. Blend fallback is similarly suppressed for this representable fullscreen-WIN0 configuration. OBJ-mosaic fallback requires active mosaic and an OAM object with its mosaic bit set.

By contrast, run 2's older ELF logged 160 fallback rows per bridge frame and reason `0x15` (window + blend + OBJ mosaic) despite completing frame 201. This is consistent with an older/conservative classifier that did not recognize the fullscreen WIN0 case. The later `native_eligible` build's Dreamcast log records frame 201 with 160 native, zero fallback, zero window/blend/mosaic reasons, and a passing capture (38,400 pixels, zero mismatches). This directly establishes the newer build's runtime behavior.

The old run's PPU state hash (`12f36c915005eac5`) and software RGB888/RGB565 hashes (`e6b64e95140b714f`, `0d585ef913d00d74`) match the later native-eligible hardware run. This makes materially different visible render state less likely as the cause, but these hashes are not a full CPU/device/IO state-equivalence proof. Run 2 did not record an independent reference-run CPU, memory, and IO fingerprint set. That remains an explicit validation gap.

**Assessment:** different build/classifier behavior is the best-supported explanation; incomplete full guest-state equivalence prevents ruling out a runtime-state contribution absolutely. The host probe is not shown to omit the relevant window/blend/mosaic state: it feeds live scanline DISPCNT, IO, and OAM to the same classifier. It remains only an upper-bound probe because it does not run the native backend.

## Capture build suitability

The native-eligible build is proven capture-capable by its recorded hardware output: it emits `SMA2_NATIVE_PVR_CAPTURE_COUNTERS` and `SMA2_NATIVE_PVR_CAPTURE`, restores the frame-200 checkpoint, captures guest frame 201, and reports stable front/back ownership and zero mismatches against the same-run software oracle. Its build record specifies capture start frame 201, PVR enabled, software composition enabled, native-skip disabled, and force-fallback disabled. This is the required mode for the emulator experiment; do not use the older `4eb…` ELF.

The prepared strip-debug ELF is 2,159,628 bytes. `sh-elf-objcopy -O binary` comparison produced the same loadable-image SHA-256 before and after stripping: `784fc9a8c8b5684cf83afa34644bfbe501a49ec15d6d79611535da7aca1feebd`. The exact emulator and input hashes are recorded in `evidence/sma2_nulldc3_frame201_offline_audit_20261019/artifact_sha256.txt`. The established nullDC3 executable hash is `f071573357a70b60d2e6ba6120fe83c475e45833c2a8970a8256382cab978b75`.

The prepared command uses absolute paths, nullDC3's established `--portable`/comma-separated config syntax, refsw2, and the staged frame-200 inputs. It is saved as `evidence/sma2_nulldc3_frame201_offline_audit_20261019/prepared_not_run_command.txt` and was **not executed**. `Exit.FramePng` remains diagnostic only; it is not the correctness capture source.

## Recommended gate before any future launch

Before launch, record an independent host reference fingerprint set for the exact checkpoint/neutral-input frame 201 and compare every field available from the emulator log: frame, PC, cycles, CPU, memory/device, and IO. Do not infer equivalence from the PPU FNV alone. Then, only with separate launch authorization, use the prepared capture-capable stripped ELF and stop on first error. Require nonzero native rows and the exact emulator-specific refsw2 color model comparison against captured PVR pixels; a fallback-only pass does not qualify.

No source, emulator, KOS, or renderer changes were made. No build was performed in this audit. Existing worktree changes, evidence, and `NEXT_TASK.md` were left untouched.
