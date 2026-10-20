# Reconstructing the validated GBARecomp tree (verified 2026-10-20)

Result: branch `recovery/sma2-validated-deps-20261020`, commit `2d2a8fe` in the
isolated checkout `/tmp/gbarecomp-recovery-20261020` (parent `bdaefa2`). Not pushed.
**/tmp is not durable**: if the checkout is lost, redo the commands below, or
`git bundle` it. The original `/home/gpf/code/gbarecomp` index, tree and stash were
not modified (still 41 status lines, `stash@{0}` present).

```
S=docs/results/evidence/gbarecomp_worktree_snapshot_20261020   # in SMA2 repo
git clone --no-hardlinks /home/gpf/code/gbarecomp /tmp/gbarecomp-recovery-20261020
cd /tmp/gbarecomp-recovery-20261020
git checkout -b recovery/sma2-validated-deps-20261020 bdaefa236f5cd344a895f2ab692cae23f0d48ef2
git clone --no-hardlinks /home/gpf/code/gbarecomp/external/arm-recomp-core external/arm-recomp-core
git -C external/arm-recomp-core checkout 61cdc51a85efe621841458d9e6c35c5357fcb2ee
git apply --index $S/index_staged.patch && git apply $S/unstaged_vs_index.patch
tar xzf $S/untracked_src_tests.tgz
```
Dependencies: submodule `external/arm-recomp-core` @ `61cdc51a` (the only
submodule the build needs; `rbengine`, `recomp-net`, Android SDL were uninitialized
in the original too); toml++ at `/usr/include/toml++`.

Checks (measured): sha256 of every modified/untracked `src/` and `tests/` file
matches the original; `cmake -DCMAKE_BUILD_TYPE=Release` + build + `ctest`: 62/62 pass.
SMA2 build: `GBARECOMP_ROOT=/tmp/gbarecomp-recovery-20261020 SMA2_DC_BUILD_DIR=... 
SMA2_GENERATED_DIR=<empty dir> SMA2_SH4_DYNAREC=ON tools/build_dreamcast.sh`
(KOS sourced by the script, GCC 17). CMake cache flags equal the accepted build
(DYNAREC=ON; AOT_POC, RGB565, IWRAM_AOT, CART_AOT_DIAG, native PVR all OFF); overlay
applies `apply_no_tls()`; no `PT_TLS`. Stripped ELF SHA-256 
`347126a2c669c7e2f15d718429d0ed006cb323c1c2f75237b2ad071fa67588f5`, identical to the
accepted run (unstripped differs: `bac358b8…` vs `41b2051e…`, build-path debug info).
nullDC3 run `/tmp/sma2-dynarec-nulldc3-recovery-20261020-run1`: verdict PASS, emulator
status 0, stages 1/2/3/D/E each 1 native block, 0 fallback, 0 mismatches.

Remaining issues: (1) the 343-entry AOT build was not rebuilt here; only the dynarec
path was reproduced. (2) Reproduction still needs the SMA2 repo's own uncommitted
working tree (`src/main.cpp`, `CMakeLists.txt`, etc.). (3) `tests/dma/bulk_test_main.cpp`
has trailing whitespace (`git diff --check` flags 2 lines), kept for fidelity.
(4) nullDC3 and the mGBADC copy (`/tmp/sma2-bg0-mgbadc-source`) are still outside
preservation records. (5) The recovery commit is unreviewed by upstream and not pushed.

## Preservation in the original repository (2026-10-20)

- Branch `recovery/sma2-validated-deps-20261020` created in `/home/gpf/code/gbarecomp`
  at commit `2d2a8fe94500ac64b9ac8f1b4250a1cfaa1546cf` (parent `bdaefa236f5cd344a895f2ab692cae23f0d48ef2`)
  by a non-forced fetch from the isolated checkout. It was absent beforehand; the
  original HEAD (`main` @ `bdaefa2`), index, status and stash list are unchanged.
- Bundle: `/home/gpf/code/backups/gbarecomp-recovery-sma2-validated-deps-20261020.bundle`,
  SHA-256 `eb260f014e9a048cf2cfebb8954cbe88028f6afe342d9f3416eb907f48d58a4f`.
  `git bundle verify` reports complete history; a clone of the bundle contains the
  recovery commit. The bundle holds only the superproject: the `external/arm-recomp-core`
  submodule @ `61cdc51a` is a gitlink and is NOT in it.

## SMA2 uncommitted-dependency assessment (measured)

Committed SMA2 `HEAD` (`git archive`) does **not** build the accepted dynarec ELF:
`prepare_dreamcast_framework.py` fails "expected one patch site ... gba_ppu.cpp".
A tree of `HEAD` plus only the following working-tree files built an ELF whose
stripped SHA-256 is `347126a2…588f5` (identical to the accepted run):

- modified tracked: `CMakeLists.txt`, `src/main.cpp`, `src/dreamcast_host_window.inc`,
  `tools/build_dreamcast.sh`, `tools/prepare_dreamcast_framework.py`
- untracked, required: `src/sh4jit/` (SH-4 backend), `vendor/mgba-sh4-jit/` (740 KB
  vendored mGBA JIT; provenance script `tools/vendor_mgba_sh4_jit.sh`, source is the
  sibling repo `brucemGBA`), and the top-level untracked `src/*.cpp|h` referenced by
  CMake (native PVR bridge, presenter, IWRAM AOT, diag, gap-pad files)
- not needed for this build: `.gitignore`, docs, `NEXT_TASK.md`, `tools/research`
  (53 MB), `src/snapshots/`, other `tools/` tests and the 246 untracked docs.
Not pruned file by file (no bisection), so some listed untracked `src` files may be
unused by this configuration.
