# GBARecomp working-tree snapshot — 2026-10-20 (read-only capture)

Captured from `/home/gpf/code/gbarecomp` at HEAD recorded in `HEAD.txt` with
read-only git commands; the GBARecomp index, working tree and stash were not
modified. Safety stash `stash@{0}` (`pre-upstream-merge-2026-10-19-gbarecomp-main`)
is recorded in `stashes.txt` and still exists.

- `tracked_vs_HEAD.patch`: all tracked changes vs HEAD (what the overlay build reads).
- `index_staged.patch` / `unstaged_vs_index.patch`: the same split by index state
  (the `MM` files differ between the two).
- `untracked.sha256`, `untracked_src_tests.tgz`: untracked files; the tarball holds
  the source GBARecomp's `CMakeLists.txt:407` requires
  (`src/runtime/iwram_code_guard.{cpp,h}`) plus `tests/irq_bridge`, `tests/iwram_guard`.

Attribution: the validated SMA2 builds compile this whole tree, and no per-hunk
bisection has been done, so which hunks are strictly required for the recovery is
**not established**. Established: `iwram_code_guard.*` is untracked yet referenced by a
modified CMakeLists, so a clean checkout of GBARecomp HEAD cannot build SMA2 as validated.
To resolve: commit on a GBARecomp branch with the owner's authorization, after
rebuilding the dynarec ELF with each candidate hunk group reverted. The TLS workaround
in SMA2 compensates for `thread_local` in `runtime_arm.h`/`gba_bus.cpp` in this tree.
