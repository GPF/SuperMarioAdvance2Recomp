# SMA2 Dreamcast Memory Feasibility

## Measured artifacts

The first isolated KOS probe produced a 296-byte generated function. Its final static ELF had `.text` size 180,248 bytes, including roughly 179.7 KB pulled in from KOS `libkallisti` even though the fixture called no KOS API. This is a link-floor measurement, not a game-size estimate. The report records the remaining ELF sections and artifact details.

A separate Linux x86-64 trial artifact reported `.text` 43,332,802 bytes, `.rodata` 2,548,612 bytes, `.data.rel.ro` 5,290,864 bytes, `.data` 3,160 bytes, and `.bss` 1,292,064 bytes. These host sizes cannot be converted into SH-4 estimates.

## Constraints and unknowns

- Retail Dreamcast main RAM is 16 MiB. The local KOS sources also describe optional 32 MiB configurations; do not treat that as a retail baseline.
- The KOS link script's default text placement is a layout clue, not proof that a game image fits.
- The shared runtime has been observed loading the full ROM into a byte vector. If retained, that consumes ROM-sized RAM in addition to executable/data, CPU state, graphics/audio buffers, stacks, KOS, and allocator overhead.
- SMA2 has two `code_copy` hints of 0x360 and 0x800 bytes; those do not bound all mutable or RAM-executed code.
- The B6 ELF/map is a link measurement, not a runnable full-game result; ROM streaming policy
  and full-game hardware memory remain unmeasured.

**Current status: NO-GO for the full static corpus on a 16 MiB retail Dreamcast.** Earlier
compile-stage accounting (superseded by the B6 link below) measured **38,343,560 bytes (36.57 MiB) of generated
`.text`** across all 64 shards. Adding the 3,628,132-byte dispatch table, 4,194,304-byte
resident ROM, and 178,924-byte KOS `.text` link floor gives a **46,344,920-byte
(44.20 MiB) compile-stage estimate**, about 2.76× retail main RAM before runtime,
data/BSS, buffers, stacks, and allocator overhead. This is not a linked game image or
peak-RAM result; the compile objects contain fat LTO data.
The compile succeeded with 128,401 warnings. Commands, per-shard sizes, logs, and objects
are preserved under `/tmp/sma2-sh4-all64-kosflags-20261015/`. An earlier compile without
KOS target flags is invalid for SH-4 and is preserved separately. Dreamcast VRAM is a
separate 8 MiB pool, not part of main RAM (KOS [ASIC memory map](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/asic.c)).
The first reduced reset-root selection is measured at two generated functions (1,468 B
SH-4 `.text`) and 13 dispatch rows (160 B including the table-length word). It was
initially a SIZE-ONLY result with no bridge or interpreter ELF; that earlier measurement
is preserved at [reduced corpus measurement](results/SH4_REDUCED_CORPUS_MEASUREMENT.md).
Gate D3 later produced a distinct bus-bound five-fixture ELF with production interpreter
bridge, bus, PPU, and BIOS and measured it on physical hardware. See the [D3 hardware
report](results/SH4_GATE_D3_HARDWARE.md). Neither artifact projects full-game size.

The D3 production-runtime fixture was linked and measured on physical Dreamcast hardware.
Its ELF has `.text` 912,312 B, `.rodata` 47,356 B, `.data` 5,784 B, and `.bss` 1,071,236 B;
the PT_LOAD `p_memsz` sum is 2,148,314 B. KOS allocator telemetry measured 5,877,760 B
(5.60 MiB) of further heap-growth headroom after the harness, plus about 1 KiB free in the
allocator arena. This is a minimal five-fixture runtime result, not a full-game fit result;
the 64 KiB stack reserve is known but stack high-water was not measured. See the
[D3 hardware report](results/SH4_GATE_D3_HARDWARE.md) for segment accounting, BIOS/ROM
residency, allocator measurements, and the precise limits.

The follow-up trace-enabled fixture ELF used a bounded 32-entry production fingerprint ring
(2,560 B) and a 2,560-byte harness stack copy buffer. Its measured sections were `.text`
912,504 B, `.rodata` 47,588 B, `.data` 5,784 B, and `.bss` 1,071,248 B, with 2,143,526 B
of PT_LOAD `p_memsz`. Physical allocator telemetry showed 5,877,760 B of additional
heap-growth headroom. The stack copy buffer is in addition to the known stack reserve;
actual stack high-water remains unmeasured.

The isolated function and KOS link floor alone do not establish whether SMA2 fits. The
full-corpus compile establishes NO-GO for that static corpus; selective builds still need
their own code, table, and runtime memory measurements.

## Interpreter-first SMA2 ELF (2026-10-15)

The current Dreamcast game build omits the generated cart corpus and forces the production
ARM/Thumb interpreter. Its linked ELF reports `.text` 1,145,909 B, `.data` 8,176 B, and
`.bss` 1,145,984 B (2,300,069 B combined); SHA-256:
`f2b3816dab86d9e2a898d20386f32547dbee6feab4862e0a28481c31902bd36f`. This is a real
game link measurement, but not peak main-RAM use. The 4 MiB ROM, BIOS, EWRAM/IWRAM,
runtime objects, 512 KiB thread stack, KOS allocations, and graphics/audio resources
remain part of the live footprint and have not been fully accounted for on hardware.
The interpreter-first ELF has booted the retail ROM and displayed its startup sequence on
a physical Dreamcast; see [port status](SMA2_DREAMCAST_PORT_STATUS.md).

## Linked full-corpus measurement (B6, 2026-10-15)

The full 64-shard corpus, the full 302,344-row dispatch table, the production dispatch and
interpreter bridge, and the full GBA subsystem now link with **zero undefined symbols**. The
linked ELF's loadable sections total **47,159,468 bytes (44.97 MiB)**: `.text` 40,991,288 B,
`.rodata` 3,664,920 B, `.eh_frame` 2,145,952 B, `.data` 5,740 B, `.bss` 351,124 B. This is
about **2.81×** retail main RAM and replaces the earlier compile-stage estimate with a real
link measurement. Of `.text`, 38,343,560 B is the corpus and **2,647,728 B** is the runtime,
full GBA subsystem, and KOS — the first measured cost of the full GBA subsystem in a corpus
link. See [B6 gate link](results/B6_GATE_LINK.md) for sections, segments, blockers, and hashes.

The B7 coverage baseline shows the corpus is far larger than what a run executes: the no-input
5077-frame run reaches 2,758 of 23,042 cart functions (12.71% of measured SH-4 `.text`). In
that run, 48 functions (172,996 B) cover 90% of executed cycles and 173 functions (553,484 B)
cover 95%. In the recorded-input run, the corresponding hot sets are 86 functions (328,520 B)
for 90% and 261 functions (872,340 B) for 95%. Each hot-set size plus the measured 2,647,728 B
runtime `.text` component is arithmetically inside 16 MiB, but this is a projection, not a
linked artifact; it still needs its own table, ROM residency, fallback path for the unexecuted
85–88% of the corpus, and hardware peak measurement. Mutable
IWRAM code (10 functions from two `code_copy` regions) is in the hot set; whether it diverges
after the copy is still unmeasured. See [B7 baseline coverage](results/B7_BASELINE_COVERAGE.md).

## Budget method for future gates

Measure linked SH-4 load sections, BSS, relocation needs, ROM residency, peak heap and stack, video/audio buffers, generated-code storage, fallback/runtime allocations, and loader placement. Validate peak usage on hardware. Do not use host `.text` as a conversion factor.
