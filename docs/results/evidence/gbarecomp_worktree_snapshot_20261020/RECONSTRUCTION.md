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
