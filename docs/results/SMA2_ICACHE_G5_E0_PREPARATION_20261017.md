# SMA2 G5-E0 preparation (offline) — 2026-10-17
**Readiness: CONDITIONAL.** No link, build or compile was run. Evidence: `/home/gpf/sma2-g5-20261017/v4/{e0_proposal,e0_prep}/`.

## Phase 2: GNU ld syntax (source: `/opt/toolchains/dc/kos/utils/kos-chain/binutils-2.47/ld`)
* The ordering file is parsed with the SECTIONS statement grammar (`ldgram.y:1555-1585`, `ldmain.c:877-889`); its statements are spliced before the first wild statement of the existing `.text` (`ldlang.c:8548-8563`), as in G3b/G4.
* `FILE(SEC)` with `*archive:member` is supported: `archive_path` splits at the first `:` (`ldlang.c:337-357`); `input_statement_is_archive_path` (`ldlang.c:363-386`) matches the member part against `f->filename` and the archive part against the archive's **full path** (`bfd_get_filename(my_archive)`), not its basename. `*libc.a` is therefore a suffix/fnmatch match (`name_match`, `ldlang.c:269`), not an exact name.
* A pattern without `:` is compared with `f->filename` (`ldlang.c:476-486`), which is the full path for plain/ltrans objects but only the member name for archive members (`ldmain.c:1393`). So an unqualified `*x.o` can also hit archive members; ordsim5 models and rejects such ambiguity.
* Section names are compared with strcmp when they contain no glob characters. Lexer WILDCHAR set `ldlex.l:95` (`[_a-zA-Z0-9/.\$~-+:[],=?*^!]`): all 2,304 qualifiers and section names are inside it, none contains `? [ ] \ ^ !` or an extra `*`/`:`.
* First match wins: an already-assigned input section is skipped (`ldlang.c:2809-2811`); E0 additionally requires each piece to be matched by exactly one statement.
* Length: the NAME token is `{WILDCHAR}*` copied with `stat_strdup` (`ldlex.l:424-437`); no fixed limit found in the lexer or parser, so the five names >255 chars (max 426; longest line 447) are accepted by source reading. **Not empirically verified.**
* Caveat: the installed binary reports `2.47.20260726`; the available source is the 2.47 tarball. A snapshot difference in this code cannot be excluded offline.

## Phase 1/3 results
* `ordsim5.py` `4069da80…`, `test_ordsim5.py` `e79c26ac…`, `e0_predict.py` `0150bc18…`. Tests: ordsim5 31/31 OK (positive, duplicates, archives, long name, full-mode roundtrip, 19 negatives); frozen `test_ordsim` 12/12 and `test_v4` 50/50 still OK. validate3/4 and ordsim unchanged (`90c518a2…`, `23fc0c72…`, `2844cd28…`).
* Original proposal unchanged: ordering `f585c53b…`, manifest `647f2037…`.
* `e0_predict.out` (`17425069…`): 11/11 PASS vs the Phase-1 map/ELF `63f48c60…` (g2-reduced). 2,305 pieces each claimed exactly once; order and every address equal Phase 1; `.text` end 0x8c108ca4, size 0xf8ca4; pads = the two input-alignment pads already in Phase 1 (12 B at 0x8c102fd4, 4 B at 0x8c10846c), equal to the gaps found by an independent map reader; 3 PT_LOAD. The approachB identity map has an identical piece table. New prediction `e0_prediction_v5.json` `e26c8d61…` (new file, original untouched).

## Proposed link (NOT run): `e0_prep/e0_run.sh.PROPOSED` (`69c68301…`)
Token-for-token G5-L1 `g5ls_run.sh` except: ordering = `/home/gpf/sma2-g5-20261017/v4/e0_proposal/e0_identity_full.ord`; outputs `SuperMarioWorldRecomp.e0.{elf,map}`; strace log `e0_execve.log`.
### Pre-link checks
G5-L1 `prelink_verify.py` set (70 plugin inputs, all earlier SHA256SUMS, Phase-1 ELF `63f48c60`, 27 frozen v3 hashes), plus: ordering `f585c53b`, `e0_prediction_v5.json`, ordsim5/test/e0_predict hashes above, `e0_predict.out` re-run PASS, output names absent, `-plugin-opt=-save-temps` and `/bin/false`, predicted image end < 0x8d000000.
### Acceptance
`loadimage_cmp` Phase-1 vs E0: entry, every PT_LOAD address/size/flags/align and file-backed sha256 identical; `.text` end 0x8c108ca4; validate4 (old = Phase 1, new = E0, identity spec) 16/16 PASS with zero deltas; archive-member inclusion section identical; execve log only `sh-elf-ld`; full-ELF `cmp` reported informationally. Any mismatch = NO-GO, no retry.
## Remaining uncertainties
Snapshot-vs-tarball ld source difference; >255-char names and archive qualifiers untested in the real binary; the `.text` ordering relies on the proven G3b/G4 splice behavior but with 2,304 statements instead of ≤60.
