# SMA2 G5-E0: single identity link — 2026-10-17
**Verdict: PASS.** One ld-only link, exit 0. The E0 ELF is byte-identical to the Phase 1 ELF (`63f48c60…`, full-ELF identity informational). Evidence: `/home/gpf/sma2-g5-20261017/v4/e0_exec/` (`SHA256SUMS.txt`). No retry, no second link, ordering file unmodified. Per the authorization, nothing beyond this link (SAFE-section move, G5-PM, hardware) was started.

## 1. Pre-link gate (`prelink_verification.log`, `e4eedd8b…`) — ALL PRECONDITIONS OK
70/70 plugin inputs (15 ltrans + 55 `debug.temp`) match preserved hashes; all earlier SHA256SUMS sets, reference ELF, Phase-1 ELF `63f48c60…`, G4 ELF, 27 frozen v3 hashes, validate3/validate4/ordsim, G5-L1 evidence unchanged. Full SHA-256 verified (`FROZEN_E0_SHA256SUMS.txt`): ordering `f585c53ba64c0954adfac1c741e6d88a074e630b9fd92a10e043d605a322040a`, proposal manifest `647f2037ca7f6c789fa9e9c1c120b1022fd36c97e4415fb72ed881e8300af631`, ordsim5 `4069da80a3fd…8edb`, test_ordsim5 `e79c26ac3a3d…95ce`, e0_predict `0150bc184c40…2814`, prediction `e26c8d618e478cbaf646cc1ba6af6d063cdd9f5f2fecf342b8d72ae78f64a961`, runner `69c68301b5a51d55b58b1a49ef3375296bd28f2ab29f9c0a02b9d671aee8193d`. ordsim5 31/31 and frozen ordsim 12/12 tests OK; e0_predict 11/11 PASS (2,305 unique pieces, Phase-1 addresses, text end 0x8c108ca4) and its regenerated manifest equals the frozen one. Runner == G5-L1 runner modulo ordering path/output names/log name; `-plugin-opt=-save-temps`, `-plugin-opt=/bin/false`, strace wrapper present; output paths absent; predicted image end 0x8c264b80 < 0x8d000000.
**Disclosure:** my first gate run (`…attempt1_checker_label_bug.log`) reported BLOCKED because my new check expected manifest kind `debug` while the manifest says `debug.temp`. The 70/70 hash check in that run had passed and no input had changed. I corrected only that label (`e0_prelink_verify.py` vs `…_attempt1.py`), re-ran the whole gate, got ALL OK, and only then linked.

## 2. Executed command
`bash /home/gpf/sma2-g5-20261017/v4/e0_prep/e0_run.sh.PROPOSED` (cwd inside the script = preserved link dir), i.e. `strace -f -e trace=execve -o …/e0_execve.log sh-elf-ld -plugin liblto_plugin.so -plugin-opt=/bin/false -plugin-opt=-save-temps -plugin-opt=-ltrans-objects=…/manifest70.txt --section-ordering-file=/home/gpf/sma2-g5-20261017/v4/e0_proposal/e0_identity_full.ord --sysroot=… -m shlelf -o SuperMarioWorldRecomp.e0.elf … -Map=…/SuperMarioWorldRecomp.e0.map … -T …/shlelf.xc` (full text in `e0_run.sh.PROPOSED`). Tools: GNU ld 2.47.20260726, Python 3.12.3, strace 6.8.

## 3. Link result
Exit status 0; stdout/stderr empty (0 bytes); wall 1.49 s; the execve log holds one exec (`sh-elf-ld`) and no cc1/lto1/collect2/lto-wrapper/gcc/as. 70/70 plugin inputs byte-identical afterwards. The GNU ld ordering file with 2,304 qualified statements, `*archive:member` syntax and 5 names >255 chars was accepted.

## 4–6. Validation (`postlink_validation.log`, `65b8e005…`) — all PASS
* Archive inclusion (frozen G-c definition): 566 entries in both, identical ordered sequence.
* `.text`: 2,305 pieces, each exactly once, same order, every address/size equal to Phase 1; end 0x8c108ca4; padding = the same two alignment gaps (12 B at 0x8c102fd4, 4 B at 0x8c10846c). Whole-map input-section list (6,004 entries, all output sections) and Discarded list identical; all 17 ELF section headers identical except file offsets (also equal in practice).
* `loadimage_cmp`: entry 0x8c010000 same; 3 PT_LOAD identical (filesz 0xf9060 / 0x35d2d / 0x1ff0; memsz …0x11cdd0 bss 0x11ade0); file-backed sha256 `783a5a1e…`, `061af9c1…`, `32c4193c…` all SAME; LOAD IMAGE IDENTICAL.
* Informational: full E0 ELF sha256 == Phase 1 ELF sha256 (`63f48c60a27a…`); `cmp -l` 0 bytes differ. The map files differ in embedded scratch paths only.
* Minor checker note: my script's entry-point check C2 captured the text "address:" for both ELFs, so it was vacuous; the entry-point result rests on `loadimage_cmp` (0x8c010000 SAME) and the byte-identical ELF.

## 7. Frozen validate4 (old = Phase 1, new = E0, spec `v3/identity_model.ord`) — `v4_validate_e0.out`: 16/16 PASS, `ALL VALIDATION GATES PASS`
S1 all 4,653 symbols as predicted; R1 20,745 relocation sites verified, 0 bad; B1 `_end` old+0x0; I1 0 unexplained bytes; E1 0 differing `.eh_frame` bytes; E3 410 LSDA pointers, 0 shifted; I2/P2 zero deltas.

## 8. Inventory (SHA-256, `SHA256SUMS.txt`)
ELF `63f48c60a27a414aee9519d727c35ac172dc53b352bf3a61006804989a5cfb88`; map `0a78dc53bffc92296a127a20549ce1b4c7985fa7a5274ee28481a5fb0e1a0202`; execve `3805cbdb81b140ffa7dab476f1cdd2e2213b0056a2e4028866dced8d79b251b7`; linker output (empty) `e3b0c442…`; loadimage_cmp `cb19e341…`; validate4 `38111135…`; post-link `65b8e005…`; pre-link `e4eedd8b…`; attempt-1 log `cf4339af…`; postlink script `ac0c51fe…`; prelink script `b1518608…`. (ELF/map/execve are also copied into the evidence dir; originals remain in the scratch link dir.)

## 9. Differences from the offline prediction
None. Placement, text end, pads and PT_LOAD equal the prediction; the only unpredicted observation is that the whole ELF is byte-identical to Phase 1.

## 10. Verdict: PASS. Stopping here; any further G5 work needs separate approval.
