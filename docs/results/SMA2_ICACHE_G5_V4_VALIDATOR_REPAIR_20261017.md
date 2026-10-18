# SMA2 G5-V4: validator repair and offline regression — 2026-10-17
**V4: PASS.** Frozen v3 and the original G5-L1 FAIL verdict are untouched. Evidence: `/home/gpf/sma2-g5-20261017/v4/` (`evidence/`, `reg/`, `neg/`, `e0_proposal/`).

* Unit tests: `test_v4.py` 50/50 OK (synthetic, no linker/compiler).
* Regression (16 gates each, all PASS): identity, G3b, G4, G5-L1 ELF (`reg/v4_*.out`). v3 on G5-L1: 11 PASS / 3 FAIL (S1, R1, E1).
* S1/R1: `_end` is an end-of-image boundary symbol (gate B1, `addrmap.py`); allocated map stays exclusive-end. `_end` 0x8c25cb80 -> 0x8c264b80 (= +0x8000); 2 relocation words reference it (S+A, addend kept); no other word within 0x1000 of the end differs.
* E1: `.eh_frame` parsed structurally (3 CIEs, 1908 FDEs, `ehcheck.py`); 2070 differing bytes, 0 outside decoded pc_begin/LSDA/personality fields. New gate E3: 410 LSDA pointers (all shifted), decoded by CIE L encoding, each == address-map(old) == offset-preserving new address in `.gcc_except_table`.
* Negative controls (byte-patched copies of the G5-L1 ELF, `neg/`): 10/10 fail on the intended gate (dir32_wrong_target, end_ref_addend_lost, end_ref_unshifted -> R1; end_sym_plus_4000, end_sym_unshifted -> S1+R1+B1; fde_instr_byte, pc_begin_off_by_4 -> E1; lsda_plus4000, lsda_unshifted, lsda_off_by_4 -> E3). Wrong-spec controls (`reg/v4_negctl_*.out`) also fail.
* Hashes: validate4 `23fc0c72…`, ehcheck `dc16f027…`, addrmap `920a26b1…`, test_v4 `f35c91bd…`, v4_negctl `19bc450d…`; validate3 `90c518a2…` and all 27 frozen v3 entries unchanged.

## E0 enumeration feasibility (read-only; nothing linked)
Measured from the Phase-1 map: 2,305 `.text` pieces (startup + 2,304), identity baseline model mismatches 0, text end 0x8c108ca4.
* 860 ltrans-owned, 1,442 archive members (libkallisti, libc, libm, libstdc++, libgcc), 2 plain objects (crtbegin.o, crtend.o).
* 66 section names occur in >1 object (168 pieces) -> must be object-qualified; (qualifier, name) is unique for all 2,304 (0 duplicates); no archive-basename collisions.
* 5 names exceed 255 chars (max name 426, max line 447). No glob/syntax characters in names. No zero-size pieces.
* **Blocker for the frozen tooling:** `ordsim.py` accepts only ltrans qualifiers, so it cannot parse or model archive-member lines. E0 needs either an ordsim extension (new version, not an edit of the frozen file) or the identity prediction taken directly from the map (done in scratch).
* **Unknown (not verifiable offline):** whether `--section-ordering-file` accepts `*archive.a:member.o(section)` patterns, and whether lines >255 chars are accepted. Only a link (or ld-only dry parse) can answer; neither was run.
* Proposed `e0_proposal/e0_identity_full.ord` (2,307 lines, sha256 `f585c53b…`) and `e0_prediction.json` (`647f2037…`).

### E0 preconditions
Separate approval; new ordsim version supporting archive qualifiers with tests; confirmation that ld parses the qualifiers and long names; Phase-0 hash gates as in G5-L1; link script identical to Phase 1 apart from the ordering path.
### Acceptance
ELF byte-identical in all PT_LOAD file-backed content to Phase 1 (loadimage_cmp), every piece at its Phase-1 address, text end 0x8c108ca4, zero pads, v4 16/16 PASS with an identity delta of zero, and archive-member inclusion identical.
