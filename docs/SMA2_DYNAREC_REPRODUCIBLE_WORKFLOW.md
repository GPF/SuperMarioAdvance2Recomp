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
