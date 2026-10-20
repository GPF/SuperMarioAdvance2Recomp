# SMA2 SH-4 dynarec: reproducible build and nullDC3 test

Emulator evidence only. Not Dreamcast hardware validation.

## Which GBARecomp is used
- Nested `./gbarecomp` is a submodule gitlink (`ec778f17`, `github.com/mstan/gbarecomp`),
  **uninitialized** (empty directory, `.gitignore` lists `/gbarecomp`). No validated build
  ever used it, and that remote does not contain the recovery commit. It is left unchanged.
  Repointing it would require a commit published to a remote the project controls; do that
  only with authorization.
- All validated builds used the standalone fork `/home/gpf/code/gbarecomp` (dirty tree).
  The needed state is now commit `2d2a8fe94500ac64b9ac8f1b4250a1cfaa1546cf` on branch
  `recovery/sma2-validated-deps-20261020` (bundle:
  `/home/gpf/code/backups/gbarecomp-recovery-sma2-validated-deps-20261020.bundle`,
  SHA-256 `eb260f01…8a4f`), with `arm-recomp-core` `61cdc51a`
  (bundle `/home/gpf/code/backups/arm-recomp-core-61cdc51a.bundle`, SHA-256
  `1f52018a547138c972046574c5791feb0a5d8ff5cc5b68c3782b3177495c6afc`).
- Pins live in `tools/gbarecomp_pin.env`. `tools/build_dreamcast.sh` defaults to
  `GBARECOMP_PIN_DEFAULT_ROOT` and refuses another revision or tracked modifications
  (`SMA2_ALLOW_UNPINNED_GBARECOMP=1` bypasses it; such builds are not recovery builds).
  Override the location with `GBARECOMP_ROOT=<path>`.

## Commands
```
# 1. clean pinned GBARecomp (reads committed objects from the bundles only)
tools/setup_gbarecomp_pinned.sh                 # -> /home/gpf/code/recovery/gbarecomp-sma2-pinned

# 2. build (the script sources /opt/toolchains/dc/kos/environ.sh and binds GCC 17)
B=/home/gpf/code/recovery/build-dynarec-20261020; mkdir -p $B-empty-generated
SMA2_DC_BUILD_DIR=$B SMA2_GENERATED_DIR=$B-empty-generated SMA2_SH4_DYNAREC=ON JOBS=8 \
  tools/build_dreamcast.sh
sh-elf-strip --strip-debug -o $B/SuperMarioWorldRecomp.stripped.elf $B/SuperMarioWorldRecomp.elf
sha256sum $B/SuperMarioWorldRecomp.stripped.elf   # expect 347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5
```
The empty generated directory selects the empty static dispatch table (no AOT), so
the diagnostic can only exercise the runtime dynarec. Everything else is OFF by default.

**Incremental rebuild:** edit a source and rerun the same build command with the same
`SMA2_DC_BUILD_DIR`; the script re-stages the overlay and re-runs `kos-cmake`, and make
rebuilds only changed objects (measured: 80 s, same stripped hash with no source change).
Delete the build dir if the overlay or GBARecomp pin changes.

```
# 3. run once under nullDC3 (REIOS loads the stripped ELF; assets staged as COPIES)
python3 tools/run_nulldc3_dynarec_diagnostic.py \
  --emulator /home/gpf/code/nulldc3-dcload-pc/build/nulldc3 \
  --elf $B/SuperMarioWorldRecomp.stripped.elf --elf-sha256 347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5 \
  --game-toml game.toml \
  --bios <gba_bios.bin> --bios-sha256 fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570 \
  --rom <super_mario_world_usa.gba> --rom-sha256 63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3 \
  --run-dir <NEW dir> --timeout-seconds 180
```

## Reading the result (`<run-dir>/result.json`, `console.txt`)
PASS requires all of: `guest_diagnostic_verdict=PASS`, `final_summary_seen`,
`backend_initialized`, and each of stage1/2/3/D/E `PASS` with `blocks>0`,
`fallback=0`, `mismatches=0`. `emulator_process_status` is separate and 0 alone is
never PASS. A `Looks like null pointer read` marker is a FAIL. `guest_program_return_status`
is not exposed by nullDC3. Check `console.txt` for `[sma2] main entered` (startup passed)
and `[sma2-jit] backend init succeeded`.

## Known pitfalls
- `PT_TLS`: see `docs/results/SMA2_DYNAREC_PT_TLS_STARTUP_POSTMORTEM_20261020.md`.
  `.tbss` overlapping `.ctors` is legal; nullDC3's loader mis-fills it. The overlay's
  `apply_no_tls()` is valid only while the guest runtime is effectively single-threaded.
- nullDC3's `/pc` rejects symlinks; stage copies.

## Measured reference (2026-10-20)
Clean build from this branch: stripped ELF `347126a2…588f5` (identical to the accepted
run and to the reconstruction). nullDC3 run: PASS, five stages, one native block each,
zero fallback. Evidence: `docs/results/evidence/sma2_dynarec_reproducible_20261020/`.
Source archive of the pre-recovery dirty tree:
`/home/gpf/code/backups/sma2-dynarec-minimal-src-20261020.tar.gz`
(SHA-256 `0996ddc01dfd0ce45f2e9067b1dc47c1c475edf10d81cb5d3e9bfe6eccfe80ab`).

## Canonical workflow: nested submodule (validated 2026-10-20)
Published branch: `GPF/SuperMarioAdvance2Recomp` `migration/gbarecomp-submodule-20261020`
(`31b2cf9`). `.gitmodules`: `gbarecomp` -> `https://github.com/GPF/gbarecomp.git`, gitlink
`2d2a8fe94500ac64b9ac8f1b4250a1cfaa1546cf`; its `external/arm-recomp-core` resolves through
`../arm-recomp-core.git` to the fork `https://github.com/GPF/arm-recomp-core.git` @
`61cdc51a85efe621841458d9e6c35c5357fcb2ee`. No URL workaround is needed. Initialize only
these two submodules (not `--recursive`: rbengine, recomp-net, Android SDL, recomp-ui are unused here).
```
git clone -b migration/gbarecomp-submodule-20261020 https://github.com/GPF/SuperMarioAdvance2Recomp.git sma2
cd sma2
git submodule update --init gbarecomp
git -C gbarecomp submodule update --init external/arm-recomp-core
B=$PWD/../build-dynarec; mkdir -p $B-empty-generated
SMA2_DC_BUILD_DIR=$B SMA2_GENERATED_DIR=$B-empty-generated SMA2_SH4_DYNAREC=ON JOBS=8 tools/build_dreamcast.sh
sh-elf-strip --strip-debug -o $B/SuperMarioWorldRecomp.stripped.elf $B/SuperMarioWorldRecomp.elf
sha256sum $B/SuperMarioWorldRecomp.stripped.elf   # 347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5
```
Run the bounded diagnostic once (new, unused `--run-dir`); this is the loaded-host form:
```
python3 tools/run_nulldc3_dynarec_diagnostic.py \
  --emulator /home/gpf/code/nulldc3-dcload-pc/build/nulldc3 \
  --elf $B/SuperMarioWorldRecomp.stripped.elf \
  --elf-sha256 347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5 \
  --game-toml game.toml \
  --bios <gba_bios.bin> --bios-sha256 fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570 \
  --rom <super_mario_world_usa.gba> --rom-sha256 63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3 \
  --run-dir <NEW dir> --timeout-seconds 300 --post-summary-grace-seconds 240
```
`--post-summary-grace-seconds` defaults to 10 (unchanged behavior).

**Three different things; never conflate them**
1. *Guest diagnostic PASS*: the guest printed its summary and each of stage1/2/3/D/E is `PASS`
   with `blocks>0`, `fallback=0`, `mismatches=0` (this is what proves the dynarec ran).
2. *Emulator exit status*: must be 0, i.e. nullDC3 exited by itself (`Exit.AfterSeconds`).
   Status 0 with no guest summary is still FAIL.
3. *Runner timeout/termination*: if nullDC3 has not exited within the grace period the runner
   terminates it (status -15) and the verdict is FAIL even though (1) holds. This happens on a
   loaded host; rerun with the longer grace rather than treating it as a guest failure, and
   record the FAIL run.
The final runner verdict is PASS only when (1) and (2) both hold and (3) did not occur.
`build_dreamcast.sh` defaults to `./gbarecomp` and refuses a different GBARecomp/arm-recomp-core
revision or tracked modifications. The bundle-based fallback in `tools/gbarecomp_pin.env` is
retained until this workflow has been used for a while.

### Measured fresh-clone validation (evidence: `docs/results/evidence/sma2_fresh_clone_validation_20261020/`)
- Fresh HTTPS clone of the published branch; submodules at 2d2a8fe / 61cdc51a; clean tree.
- Build from committed sources: rc 0, stripped ELF `347126a2…588f5` (identical), no `PT_TLS`.
- Incremental: rerun (87 s) and after `touch src/main.cpp` (86 s, main.cpp rebuilt): same hash.
- nullDC3 run1 and run2: all five stages PASS (1 native block, 0 fallback, 0 mismatches) and
  the summary was seen, but the **runner verdict was FAIL** (emulator status -15). Cause: the
  machine was loaded (load avg ~11, unrelated processes), so nullDC3 needed more than the
  runner's fixed 10 s post-summary grace to exit by itself and the runner terminated it.
  Recorded as FAIL; not a guest defect.
- Run3 with `--post-summary-grace-seconds 240` (new option; default stays 10): emulator
  exited by itself, status 0 (wall 28 s), verdict **PASS**, same five stages PASS.
- After the summary the console shows a `Looks like null pointer read ... PC: 0x00000002` and
  a REIOS reboot. This appears in every run including the earlier PASS runs; it follows guest
  exit and is not a startup fault. The runner only treats the marker as fatal before the summary.
