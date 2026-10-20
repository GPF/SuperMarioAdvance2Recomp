# SMA2 Project Agent Rules

- Keep SMA2-specific startup, configuration, generated game code, coverage, and port status in this repository. Keep reusable ARM/Thumb analysis, SH-4 backend, GBA CPU/runtime services, and platform APIs in GBARecomp.
- Treat source, logs, and earlier agent reports as evidence only for what they directly show. Cite paths and line numbers for source claims; label measurements, inference, hypotheses, and unknowns.
- Preserve upstream GBARecomp compatibility. Keep experiments isolated and reversible, and preserve unrelated working-tree changes.
- Keep GBARecomp as a separate submodule dependency. Do not initialize, update, or repin it without explicit authorization; do not copy its shared runtime into SMA2.
- Establish ARM/Thumb semantics, state ownership, branch behavior, and observable memory effects before optimizing.
- Validate on the host before Dreamcast hardware. A PC build or emulator run is not Dreamcast support.
- Before any Dreamcast/KallistiOS build command, source `/opt/toolchains/dc/kos/environ.sh` in the same shell and verify `command -v sh-elf-gcc` resolves to the installed compiler before invoking the build. On this machine GCC is at `/opt/toolchains/dc/sh-elf/17exp-sh-elf`; `environ.sh` still defaults `KOS_CC_BASE` to the parent directory, so follow `docs/BUILDING_DREAMCAST.md` and the recorded toolchain override when needed. Check whether a build wrapper re-sources the environment and resets that override. Do not diagnose a missing compiler from the stale default path, install another compiler, or guess a toolchain location.
- Track cycles and externally observable memory behavior at the CPU/runtime boundary.
- Never hide interpreter or JIT fallback. Report native and fallback coverage explicitly.
- Track generated code size, ROM residency, RAM use, and peak allocations from measured artifacts. Do not infer SH-4 output size from host binaries.
- Do not add speculative SH-4 optimization without measurements.
- Do not modify sibling repositories without explicit authorization. Shared framework work belongs in GBARecomp; do not copy mGBADC, gpSP, or Bruce runtime code into this title project.
- Claim Dreamcast support only after validation on physical hardware with recorded configuration and results.
- Treat interpreter, static generated AOT, runtime SH-4 dynarec, and native RAM-copy templates as separate execution paths; require path-specific runtime instrumentation before claiming any path ran.
- Treat nullDC3 results as emulator evidence only. Record the guest diagnostic verdict separately from the emulator process status, and reject missing verdicts even when the emulator exits 0.
- Use explicit GO/NO-GO gates. Stop on correctness mismatches, hidden fallback, unresolved ABI/relocation requirements, or a demonstrated memory-budget failure.

# Repository Boundaries and Preservation

- `NEXT_TASK.md` is an append-at-top handoff log. Add the newest handoff above the preserved history; never overwrite, truncate, or replace the file. Keep recorded statuses (HOLD, CONDITIONAL GO, NO-GO, FAIL, NULL, default-OFF) exactly as written; do not promote an experiment to production-ready without a recorded, validated gate.
- Validated builds depend on state outside this repository: the `/home/gpf/code/gbarecomp` working tree (including its uncommitted IWRAM guard and PPU/barrier hooks), the source-pinned mGBADC copy used by the native PVR renderer, and the `/home/gpf/code/nulldc3-dcload-pc` emulator build. Record their revisions, hashes, and a patch or snapshot in this repository's preservation records before relying on them. Do not rely on `/tmp` as the only copy of any validated input.
- Never commit ROMs, BIOS files, save files, or copies of them. `.gitignore` also hides `*.log`, `*.csv`, `*.png`, and `*.bin`; evidence that depends on those files needs an explicit decision, not a silent skip.
- Do not stage, commit, reset, clean, or stash the working tree without explicit authorization; it holds a large amount of untracked source and evidence that exists nowhere else.
