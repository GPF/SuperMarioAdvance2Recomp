# External Dependency Preservation Manifest

Recorded 2026-10-19. This file documents where the state of three source trees that
validated SMA2 builds and emulator runs depend on was preserved, how to verify it, and
what is still not covered. It stores no archives or binaries; it points to them.

**Clean-checkout reproducibility has NOT been established.** The preservation below
captures source state and was checked offline (patch applies, archive contents equal live
files, hashes verify). No build was performed from the preserved copies, and the
SMA2 repository itself does not yet contain the AOT, PVR or IWRAM source that those
builds need (see "Remaining gaps").

## Location and verification

Directory (outside this repository, not backed up by it):
`/home/gpf/code/_sma2-dependency-preservation-20261019/` (31 MB).

```
cd /home/gpf/code/_sma2-dependency-preservation-20261019
sha256sum -c --quiet SHA256SUMS.txt        # 32 files; must print nothing
sha256sum SHA256SUMS.txt                   # expect 345f74836c493babe8ddfe1a0bb730ea05fe8df3381ecefe0d2150d1d3cc6023
```

`README.md` in that directory repeats the restore notes below.

## Preserved sources

| Tree | Base revision | Preserved as | SHA-256 |
|---|---|---|---|
| `/home/gpf/code/gbarecomp` (dirty `main`) | `5e89943528aee7bc1c399bdbd035364a4b41dd10`; submodule `external/arm-recomp-core` `1f13cdfd3b437a7a3919285f381987bd43f5234a` (clean) | `gbarecomp/tracked-changes-vs-5e89943.diff` (19 files) | `9a7a5f0d31255c47be883ace5ad0565d458ac7e09a658729b0e603c91b1000ee` |
| same | same | `gbarecomp/gbarecomp-5e89943-worktree-snapshot.tar.gz` (490 files, includes untracked `src/runtime/iwram_code_guard.{cpp,h}` and `tests/iwram_guard/`) | `57d1630a9002743b8c3a51f5de3e26175b57d60ea46ad2645e4d5b7c2ea004ce` |
| `/tmp/sma2-bg0-mgbadc-source` (not a git repo; source-pinned mGBADC copy used by the native PVR build) | none recorded; original checkout `/home/gpf/code/dreamcast/mGBADC` at `ece070e2529d480054cbc77fbfcb81a020555a14`, dirty | `mgbadc/mgbadc-pinned-source-83fd6ab6.tar.gz` (3,058 source files) | `b9c9d8aec5b19465798c2934bab58a6f6b03ac9543861af3a2526b9021823c0d` |
| same | same | `mgbadc/FULL_MANIFEST_sha256.txt` (all 7,076 files, including those not archived) | `5e65fc0eb11c3f673f3a28cc4f8d4fe854f84668f35aac4848658ecf191f217f` |
| `/home/gpf/code/nulldc3-dcload-pc` (detached HEAD) | `78d44f277a774ca11f672a926b3eaf387ebc1445` on upstream `165775e5e05c30275a1d98ec8ab2d7f6cd64f5a4`; four local, unpushed commits | `nulldc3-dcload-pc/feature-dcload-sh4-tracing-165775e5..78d44f27.bundle` | `0a2db80f3576da6e52e071f9e1022d6cd864968300bf860d0f77d18cb2f2534f` |
| same | same | `nulldc3-dcload-pc/nulldc3-dcload-pc-78d44f27-source.tar.gz` (git archive of HEAD) | `32796beb070dd31f9cbf869c2ed88f9792850fb132b93a2450c124b3430a1fed` |

Key identity hashes of validated artifacts (recorded, not archived):

- Native PVR renderer file `src/platform/dreamcast/native/dc-pvr-gba.c` in the pinned
  mGBADC copy: `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`. The original dirty
  checkout's copy differs (`2c5d3a27baf0d5ac6240ec0ecec7f321cb94495676751b52fa036b7f7103728e`).
- Built emulator `build/nulldc3`: `f071573357a70b60d2e6ba6120fe83c475e45833c2a8970a8256382cab978b75`
  (RelWithDebInfo, Ninja, `HAS_TA_LLE=ON`). Not archived.

Excluded on purpose: ROMs, BIOS files, saves, checkpoint states, generated executables, build
trees, evidence binaries, and the stray `irq_bridge_*` outputs in `gbarecomp`. The excluded
mGBADC files are listed in `mgbadc/EXCLUDED_files.txt`; nothing under its `src/` or
`include/` is excluded.

## Restore recipes (what each was checked against)

- **gbarecomp:** `git archive 5e89943528aee7bc1c399bdbd035364a4b41dd10 | tar -x -C <dir>`, then
  `git apply gbarecomp/tracked-changes-vs-5e89943.diff` in `<dir>`, then extract the untracked
  files from the snapshot tarball (`tar -xzf ... --skip-old-files`). Checked: the patch applies to a
  pristine export and the result equals the live files. Submodules are separate; `external/arm-recomp-core`
  must be at its pinned revision.
- **mGBADC pinned copy:** extract `mgbadc/mgbadc-pinned-source-83fd6ab6.tar.gz` and point
  `SMA2_MGBADC_ROOT` at it (the build default is `/tmp/sma2-bg0-mgbadc-source`). Checked: the
  extracted files verify against `mgbadc/FULL_MANIFEST_sha256.txt`.
- **nullDC3:** `git clone <local repo or upstream>`, `git fetch` the bundle, then check out
  `78d44f27`; or extract the source tarball. Configure as in `nulldc3-dcload-pc/build_config_summary.txt`.
  Checked: the bundle verifies and its head is `78d44f27`; the four patches were generated from those
  commits. Not checked: applying them elsewhere or rebuilding from them.

## Remaining gaps

- No file in this repository points at the preservation directory except this one, and the
  directory is outside Git.
- The pinned mGBADC copy has no recorded base revision; its relation to the original checkout is only
  the diffs in `mgbadc/original-to-pinned-*.diff`.
- The AOT, native PVR bridge, checkpoint, IWRAM and SH-4 sources in this repository are
  still uncommitted (as of this file's commit), and the validated builds also need the uncommitted
  `gbarecomp` changes above.
- Not preserved: other `/tmp` inputs (`/tmp/nulldc3-sh4trace` checkout, `/tmp/sma2_repro_reset`,
  `/tmp/sma2_pvr_cross_scene_build`), generated cart sources, the KOS toolchain, uninitialized
  submodules (nullDC3 `devtools` and test vendors; `gbarecomp` `rbengine` and `recomp-net`), and
  the build directories.
- The RGB565 sidecar build path pins `gbarecomp` PPU file hashes that no longer match the current files.
- Everything above describes validated default-OFF experiments; none of it is production-ready.
