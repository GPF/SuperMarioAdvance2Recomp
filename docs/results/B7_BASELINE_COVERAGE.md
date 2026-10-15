# B7 — Reproducible host baseline, coverage table, and hot set

Scope: the PC baseline corpus for SMA2, measured on host only. No Dreamcast hardware was
touched in this task. Facts are measurements from the artifacts listed in §6; anything
labelled inference or unknown is not a measurement.

## 1. Verdict

- The baseline is **reproducible**: two identical 250-frame runs produced byte-identical
  fingerprint dumps.
- The generated corpus is **much larger than what a run executes**. A 5077-frame run
  executes **2,758 of 23,042 cart functions (11.97%)**, which is **12.71%** of the measured
  SH-4 `.text`.
- Executed code is **highly concentrated**: the top 48 functions carry 90% of executed
  cycles and only **172,996 B** of SH-4 `.text`; the top 173 carry 95% for **553,484 B**.
- Mutable RAM code is present in the corpus (10 IWRAM functions from two documented
  `code_copy` regions) and is in the hot set. **Whether IWRAM code diverges after the copy
  is not measured** — see §5.

## 2. Corpus definition (measured)

| Item | Value |
|---|---|
| Cart corpus source | `/tmp/SMA2-PC-Trial/generated/recompiled_*.cpp` (64 shards) |
| Cart functions | 23,042 |
| Cart dispatch rows | 302,344 (`dispatch_table.cpp`, `kDispatchTableLen`) |
| Cart SH-4 `.text` (per-function sections, KOS flags) | 38,343,560 B |
| BIOS corpus | `/tmp/SMA2-PC-Trial/gbarecomp/generated_bios/` — 770 functions, 5,143 dispatch rows |
| BIOS SH-4 `.text` (D3 scratch object, KOS flags) | 537,004 B total sections; 331,040 B attributable to mapped functions |
| IWRAM functions in the cart corpus | 10 |
| ROM | `roms/super_mario_world_usa.gba`, sha1 `5101ddf223d1d918928fe1f306b63a42ada14a5e`, sha256 `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3` |
| `game.toml` scan range | `aot_scan_start = 0x080000C0`, `aot_scan_end = 0x080A0000`, `static_resume_all = true` |

## 3. Instrumentation and its validation

The measurement uses a **host-only scratch overlay** (`/tmp/sma2-d4/overlay/runtime_arm_agg.cpp`)
that replaces the production fingerprint ring store with a per-PC aggregate. Same call site,
same armed gate (`g_runtime_insn_trace`), same pre-execution PC, no guest state modified.

Validation against the production ring on a 250-frame window:

| Check | Result |
|---|---|
| Distinct PCs | 6,988 vs 6,988 |
| Per-PC instruction count mismatches | 0 |
| Per-PC cycle attribution mismatches | 0 |
| Total instructions | 6,948,441 (both) |
| Total attributed cycles | 70,468,260 (both) |

Determinism: two 250-frame runs produced identical ring dumps, SHA-256
`026016e59c1916275aa327594976988faa471cff43c70996797e246ee3cc6a8f`.

Cycle attribution rule: record `i` stamps state before execution, so the delta
`cycles[i+1] - cycles[i]` is attributed to the PC of record `i`.

## 4. Baseline runs

Both runs: headless, no window, `--frames 5077`, `self_heal_coverage=FULLY_STATIC`,
`dispatch_misses=0`, `interpreted_insns=0`, `healed_native=0`.

| Run | Inputs | Distinct PCs | Instructions | Attributed cycles | Cart functions executed | Cart SH-4 `.text` reached |
|---|---|---|---|---|---|---|
| A (baseline) | none | 30,470 | 194,557,534 | 1,426,335,713 | 2,758 / 23,042 | 4,872,908 B (12.71%) |
| B (synthetic input) | recorded replay | 36,343 | 236,064,402 | 1,426,356,942 | 3,298 / 23,042 | 5,655,680 B (14.75%) |

Run B inputs are an explicit recorded replay (`/tmp/sma2-d4/input_replay.txt`, 4,879 events,
Start at frame 120, then held Right with periodic A/B). It is not a capture of real hardware
input. Run A has no input events.

**Unknown:** whether either run reaches actual level gameplay. The artifacts show more code
executed under input, not that a level was entered. Run A is best described as boot plus
whatever state the game reaches with no input.

BIOS: 26 of 770 BIOS functions execute in both runs (4,740 B of the mapped BIOS `.text`).
All unmapped executed PCs in the 250-frame window are in the BIOS region (1,903 records,
0.027% of that window).

## 5. Hot set (measured, per run)

Executed-function SH-4 `.text` needed to cover a fraction of executed cycles:

| Coverage of executed cycles | Run A functions | Run A bytes | Run B functions | Run B bytes |
|---|---|---|---|---|
| 50% | 1 | 620 | 1 | 620 |
| 80% | 4 | 14,420 | 17 | 39,040 |
| 90% | 48 | 172,996 | 86 | 328,520 |
| 95% | 173 | 553,484 | 261 | 872,340 |
| 99% | 597 | 1,566,452 | 807 | 1,995,916 |

Top functions by executed cycles (run B): `0x800052E` (620 B, 55.7% of cycles),
`0x30015F4` (5,200 B), `0x80011FE` (5,544 B), `0x30016B8` (1,856 B), `0x3001708` (6,744 B).
Four of the top eight are IWRAM functions.

These are **executed-cycle** hot sets, not a residency plan. A resident-only design still
needs a fallback for the 85–88% of the corpus that a single run never reaches.

## 6. Mutable code — measured and unmeasured

`game.toml` declares two `code_copy` regions, and the corpus contains functions for both:

| Runtime start | ROM source | Size | Name |
|---|---|---|---|
| `0x03001518` | `0x0809DCDC` | 0x360 | `iwram_sound_mixer` |
| `0x03002364` | `0x080000FC` | 0x800 | `iwram_irq_and_runtime_helpers` |

10 IWRAM functions exist in the cart corpus, and they are in the hot set.

**Unmeasured:** whether IWRAM bytes diverge from the ROM source after the copy during a run.
A bus-read probe called from inside the fingerprint hook segfaults (`bus_read_u32` is not
safe at that point); the check needs a hook point outside the instruction boundary. This is
the open item for validating a resident IWRAM image.

## 7. Artifacts

| Artifact | Path | SHA-256 |
|---|---|---|
| Run A aggregate | `/tmp/sma2-d4/agg_full.tsv` | `5f104091ab43620f97cb8513a6bc774db7bc200245a9ac5618189dd6f6b0af81` |
| Run B aggregate | `/tmp/sma2-d4/agg_replay.tsv` | `f4786ab2e42927b1ab8308225f2a3dbeddc508f65fc882a1ce74f9a4d19d0bd2` |
| Input replay | `/tmp/sma2-d4/input_replay.txt` | `62282bf281b7106f7a1fdf3633afff76eee67ed4c49e1ded4890983f35d9d2d5` |
| Coverage tables | `/tmp/sma2-d4/cov_noinput.tsv`, `/tmp/sma2-d4/cov_replay.tsv` | per-function `insns`, `cycles`, `sh4_text` |
| Overlay source | `/tmp/sma2-d4/overlay/runtime_arm_agg.cpp` | scratch, not upstream |

Commands (exact):

```text
GBARECOMP_INSN_TRACE=1 GBARECOMP_D4_AGG=/tmp/sma2-d4/agg_full.tsv \
  /tmp/sma2-d4/d4_host_overlay --bios gbarecomp/bios/gba_bios.bin \
  --rom roms/super_mario_world_usa.gba game.toml --no-window --frames 5077

GBARECOMP_INSN_TRACE=1 GBARECOMP_D4_AGG=/tmp/sma2-d4/agg_replay.tsv \
  GBARECOMP_INPUT_REPLAY=/tmp/sma2-d4/input_replay.txt \
  /tmp/sma2-d4/d4_host_overlay --bios gbarecomp/bios/gba_bios.bin \
  --rom roms/super_mario_world_usa.gba game.toml --no-window --frames 5077
```

Mapping script: `/tmp/sma2-d4/coverage.py` (function entry + `end` ranges from the generated
comments, per-function SH-4 sizes from `sh-elf-readelf -SW` on the KOS-flagged objects).

## 8. Handoff

- The corpus is 23,042 cart functions; a single 5077-frame run reaches ~12–15% of its SH-4
  `.text`. Full-corpus residency is a separate decision from full-corpus execution.
- The hot set is run-dependent: Run A needs 172,996 B for 90% and 553,484 B for 95%; Run B
  needs 328,520 B for 90% and 872,340 B for 95%. The remaining corpus still needs a fallback path.
- Mutable-code residency validity is still open: the IWRAM divergence check needs a safe hook
  point.
- Nothing here is Dreamcast evidence.
