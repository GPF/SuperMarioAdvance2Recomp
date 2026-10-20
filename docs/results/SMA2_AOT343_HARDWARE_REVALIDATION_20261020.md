# 343-entry bounded AOT diagnostic — rebuilt ELF, physical Dreamcast (2026-10-20)

**Verdict: `SMA2_BOUNDED_AOT_DIAG=PASS`, `Program returned 0`; one run, no retries.** One generated Thumb
`BX LR` leaf (`0x0809DFBE`); not gameplay or general AOT correctness.

- ELF: `fresh-build-aot343-20261020/SuperMarioWorldRecomp.elf` (unstripped, uploaded as built), SHA-256
  `c657de4f8dffd0518b5fcc37caf99b64ec3fb2412ce15309033b69eacf8c900f`. Built from the migration branch
  (GBARecomp 2d2a8fe, arm-recomp-core 61cdc51a) at local commit c812e35. Dynarec OFF; diagnostic and
  isolated-only ON; replay forced OFF. It is NOT byte-identical to the previously accepted ELF
  (`31d9ed30…`): that one linked a `PT_TLS` (0x50) and was built before the TLS workaround; this one has none and
  `.text` is 0x120 bytes smaller. Cause is inferred (TLS access removal), not proven by a TLS-on rebuild.
- Corpus: exactly 343 entries (`kDispatchTableLen = 343`), `dispatch_table.cpp` SHA-256
  `69723f0a83c8b0abace0ab898fb22f02922d3e14f04c693f28501e3d77e2c109`, reused verbatim (no regeneration).
  Preserved at `/home/gpf/code/backups/sma2-aot-343-corpus-20261020.tar.gz`, SHA-256
  `77a40040c3eca45250d560ec2af7ddba59b9e8c3a80aeb13a57b2ea67518c480` (67 files). The earlier recorded
  tree hash `84078436…` used an unrecorded recipe and was not re-derived; a path-sorted
  `sha256sum | sha256sum` of the files is `2327d28aa548863e52405a781424b45841c551633512ebce8e1b189193dfa580`.
  Original generation provenance remains as in `SMA2_BOUNDED_AOT_RECOVERY_AND_DYNAREC_20261020.md`.
- Hardware: console 192.168.0.128 via `kos-tool-locked.sh` (task `sma2-aot343-hw-20261020`); lock absent before,
  released after; no loader remained. Pre-launch hashes (ELF, game.toml, BIOS, ROM, coverage json) verified and
  recorded; no `config.ini`; media staged as in the accepted run.
- Console evidence: `[sma2-cart-aot-diag] status=PASS contract=ISOLATED entry=0x0809dfbe mode=thumb opcode=4770
  native_entries=1 selected_pc=0x0809dfbe exit_native=0x080015fa exit_interp=0x080015fa cycles_native=11
  cycles_interp=11 cpu=MATCH iwram=MATCH ewram=MATCH events=CLEAR return_depth=0/0`, then
  `SMA2_BOUNDED_AOT_DIAG=PASS`, `Program returned 0`. `native_entries=1` at the selected PC is the
  path-specific proof that the generated leaf ran.

## Rebuilding (does not regenerate the corpus)
```
tar xzf /home/gpf/code/backups/sma2-aot-343-corpus-20261020.tar.gz -C <dir>   # -> <dir>/sma2-bounded-reset-a2-20261020
sha256sum <dir>/sma2-bounded-reset-a2-20261020/dispatch_table.cpp             # 69723f0a…2c109
SMA2_DC_BUILD_DIR=<build> SMA2_GENERATED_DIR=<dir>/sma2-bounded-reset-a2-20261020 \
SMA2_CART_AOT=ON SMA2_CART_AOT_REUSE_GENERATED=ON SMA2_REQUIRE_GENERATED_DISPATCH=ON \
SMA2_CART_AOT_DIAG=ON SMA2_CART_AOT_DIAG_ISOLATED_ONLY=ON JOBS=8 tools/build_dreamcast.sh
```
Without `SMA2_CART_AOT_REUSE_GENERATED=ON` the script runs `gba_recompile` and would replace the corpus.
Launch per `docs/BUILDING_DREAMCAST.md` (shared lock); `/pc` holds `game.toml`, `gbarecomp/bios/gba_bios.bin`,
`roms/super_mario_world_usa.gba`, `recomp_coverage_AA2E.json` only.
