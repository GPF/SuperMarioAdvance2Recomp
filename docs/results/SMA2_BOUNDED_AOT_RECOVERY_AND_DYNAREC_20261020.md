# SMA2 bounded AOT recovery and SH-4 dynarec status — 2026-10-20

## Verdict

The bounded AOT recovery is accepted for the one hardware-tested generated
leaf. The 343-entry corpus and its hardware evidence remain intact. The runtime
SH-4 dynarec is implemented and selectable, but this merged build has **not**
passed a nullDC3 execution test: the guest starts KOS and then faults in
`___do_global_ctors_aux` before SMA2 `main()`, before backend initialization,
and before any dynarec block executes. Dynarec validation is therefore
**NO-GO**. No Dreamcast hardware launch occurred for this work.

## Recovery sequence and prevention

The full FunctionFinder required closure contained 22,698 functions and linked
about 31 MiB of SH-4 `.text`, too large for Dreamcast main RAM. The existing
`--max-functions` option bounds optional discovery; required reachable
functions still enter the closure, so it did not cap this output. The separate
opt-in `--aot-selection-limit` now chooses a deterministic bounded set without
changing `--max-functions` semantics. The SMA2 Reset profile used here has 343
entries (3 ARM, 340 Thumb). The original full generated corpus and its complete
provenance are unavailable; the recovered set is reproducible, but is not
claimed to be the exact historical source corpus.

The reused generated output's `dispatch_table.cpp` SHA-256 is
`69723f0a83c8b0abace0ab898fb22f02922d3e14f04c693f28501e3d77e2c109`; the
recorded generated-output tree hash is
`84078436ed3f7fa68f8e5db71e36e1478d2e9ef12b6cf6aee7d0e25645ea66f2`. The
accepted hardware ELF hash is
`31d9ed306fa887f5c940ef84b8087b68d30fa448cf5bde791e29e5ef83560035`.

The first post-merge hardware diagnostic allocated a 32 MiB replay ring before
the first guest step. Setting the replay default OFF alone did not suffice:
configuration and environment overrides could turn capture back on. Diagnostic
builds now force replay capture OFF after those overrides in the generated
overlay, and SMA2 supplies a false replay default for AOT/JIT diagnostic builds
(`src/main.cpp`; `tools/prepare_dreamcast_framework.py`). The heavyweight IRQ
differential then exhausted the Dreamcast heap with 3 MiB, 3 MiB, and 790,528 B
requests. That diagnostic was not made smaller by weakening comparisons; it was
replaced for the initial AOT hardware experiment with the recovered
pre-merge one-leaf fixture.

The final-result propagation fix captures the aggregate diagnostic result,
prints exactly one `SMA2_BOUNDED_AOT_DIAG=PASS|FAIL`, and returns failure through
normal cleanup. The lightweight diagnostic directly dispatched the selected
Thumb `BX LR` at `0x0809DFBE`; physical Dreamcast reported one native execution,
11/11 native/interpreter cycles, matching CPU/PC/IWRAM/EWRAM/event/return-depth
state, `SMA2_BOUNDED_AOT_DIAG=PASS`, and `Program returned 0`. This is one leaf,
not gameplay or general AOT correctness.

Prevention rules:

- Keep explicit AOT selection separate from `--max-functions`.
- Keep replay capture disabled for Dreamcast diagnostic builds, including
  configuration and environment overrides.
- Do not infer runtime heap headroom from static ELF fit.
- Keep lightweight hardware probes separate from heavyweight host
  differential fixtures.
- Require an explicit guest PASS/FAIL and program return status.
- Require instrumentation proving the intended execution backend ran.
- Preserve exact corpus, ROM/profile, ELF, asset, toolchain, command, and log
  hashes for every accepted result.
- Keep nullDC3 evidence separate from physical Dreamcast evidence.

## Execution paths and RAM-copy templates

There are four distinct mechanisms:

1. **Interpreter:** the GBA ARM/Thumb interpreter executes instructions using
   the shared GBA bus and scheduler. The normal Dreamcast runner currently
   forces this path (`src/main.cpp`, `GBARECOMP_FORCE_INTERP=1`).
2. **Static generated AOT:** compile-time generated functions are indexed by
   guest PC in the dispatch table. The bounded 343-entry SMA2 corpus uses this
   path only when code explicitly calls `runtime_dispatch()`; ordinary startup
   remains interpreter-first. The accepted hardware diagnostic made one direct
   dispatch to the Thumb leaf.
3. **Runtime SH-4 dynarec:** with `SMA2_SH4_DYNAREC=ON` and backend selection
   `j` (`/pc/sma2_cpu_backend.txt`) or `SMA2_CPU_BACKEND=jit`, the SMA2 runner
   routes eligible ROM PCs through the vendored mGBA ARM/Thumb-to-SH-4 JIT.
   BIOS and mutable RAM are outside the JIT region policy and use the
   interpreter. The emitted SH-4 code is generated during guest execution; a
   linked backend or static AOT entry is not proof that this path ran. The
   `SMA2_SH4_AOT_POC` option is a separate compile-time static block experiment.
4. **Native RAM-copy template dispatch:** the generated corpus registers 16
   byte/mask templates covering 120 RAM entry PCs. `dispatch_resolve()` checks
   RAM bytes through the side-effect-free `native_code_view()` and selects a
   matching compiled template. This lookup is part of the static runtime
   dispatcher; it is distinct from `SMA2_IWRAM_AOT` and IWRAM islands. It can
   execute only when `runtime_dispatch()` is reached and the generated table is
   linked/registered. A normal forced-interpreter step bypasses it. The bounded
   AOT hardware leaf did not exercise a RAM template. The dynarec validation
   ELF intentionally linked the empty static dispatch table, so templates
   could not execute in that test.

Useful source locations: `CMakeLists.txt:140-155`, `src/main.cpp:16-34` and
`src/main.cpp:61-70`, `tools/prepare_dreamcast_framework.py:833-925`,
`/home/gpf/code/gbarecomp/src/armv4t/runtime_arm.cpp:880-1008,1035-1079`, and
`/home/gpf/code/gbarecomp/src/gba/gba_bus.cpp:639-660`.

## Dynarec build and nullDC3 attempt

The isolated Dreamcast build used `SMA2_SH4_DYNAREC=ON`,
`SMA2_SH4_AOT_POC=OFF`, an empty static dispatch table, replay forced OFF,
RGB565/PVR/native PVR/IWRAM AOT/islands OFF, and unrelated optimizations OFF.
The KOS cross-build completed. The resulting debug ELF SHA-256 is
`2ad5d355f5ff927a9ec663f87b7729f2c7f681b52c3c686fa08daf0843ad062c`; its
loadable SH RAM range ends below `0x8d000000`. `sh-elf-nm` found the runtime
compiler entry `_ARMJITCompile`. The isolated strip-debug derivative used for
REIOS is 2,132,932 bytes, SHA-256
`2409724314313c602b76a222f9a67ba705a0cf79dad0f6f885af13bf584bea17`; its
loadable binary hash matches the unstripped ELF's binary hash
`4cf879ff97b6341bceee54b1f60347882c04381ed312a1cba2b6178993051d9b`.
`sh-elf-size` reports 1,601,257 B text, 210,972 B data, and 1,159,552 B BSS
(2,971,781 B total). The PT_LOAD memory ranges are
`0x8c010000–0x8c153224`, `0x8c154000–0x8c197cc9`, and
`0x8c197d4c–0x8c2e66c0`, including BSS. The dynarec build intentionally used
the empty dispatch table; it did not link or alter the bounded 343-entry corpus.

The fresh-run nullDC3 script validates ELF/BIOS/ROM SHA-256 and the manifest's
BIOS/ROM SHA-1, creates a new run directory, stages assets by symlink, selects
the JIT through `/pc`, captures a PTY log, distinguishes the emulator process
status from guest diagnostic evidence, rejects missing stage output, and stops
only the process it spawned. It does not treat the emulator's status 0 as a
guest PASS. Its first full-duration attempt ended with process status 0 but no
SMA2 startup/backend/stage summary. A fail-fast rerun stopped on the logged
`Looks like null pointer read` at `PC: 0x00000002`, return PC
`0x8C153214` in `___do_global_ctors_aux`; result was FAIL, emulator status
`-15` after supervisor termination, no diagnostic entry, no backend init, and
no stage executed.

The manifest staged BIOS at `/pc/gbarecomp/bios/gba_bios.bin` (SHA-1
`300c20df6731a33952ded8c436f7f186d25d3492`, SHA-256
`fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`) and ROM
at `/pc/roms/super_mario_world_usa.gba` (SHA-1
`5101ddf223d1d918928fe1f306b63a42ada14a5e`, SHA-256
`63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`). The
nullDC SH-4 CPU was configured as interpreter (`Dynarec.Enabled=0`), so a
future successful run would still need the SMA2 log and stage telemetry to
prove execution by the guest's runtime ARM/Thumb-to-SH-4 dynarec.

The ELF's constructor table and PT_LOAD range are present and structurally
similar to a known-working project ELF. This does not establish why REIOS/nullDC3
reaches function pointer `0x00000002`; the root cause remains unknown. KOS
printed its banner and initialized devices, but SMA2 `main()` was not reached.
No runtime-generated SH-4 code ran. The diagnostic compares each eligible
native block against a private interpreter replay and reports CPU/banked state,
CPSR, PC/mode, full IWRAM, cycles, and recorded writes. Its five historical
stage comparisons require zero fallback; they do not demonstrate fallback and
reentry. EWRAM is not included in this JIT comparison. Those comparisons remain
untested against the merged runtime in nullDC3.

nullDC3 run artifacts:

- First attempt: `/tmp/sma2-dynarec-nulldc3-20261020-run3/`.
- Fail-fast attempt: `/tmp/sma2-dynarec-nulldc3-20261020-run4/`.
- Emulator executable SHA-256: `f071573357a70b60d2e6ba6120fe83c475e45833c2a8970a8256382cab978b75`.
- BIOS SHA-256: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`.
- ROM SHA-256: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`.

The framework checkout was GBARecomp HEAD
`c448b8a0218672dba935168d049779b50576213f` with `arm-recomp-core` at
`61cdc51a85efe621841458d9e6c35c5357fcb2ee`. Its working tree was dirty;
`git diff HEAD --binary` SHA-256 was
`88d2d13a6ce4ffc337bbe24bcd3e682481eaa975169405238bddc38530ae3f38`. This
build describes that local state, not a clean upstream checkout.

Host build of GBARecomp succeeded and its CTest suite passed 63/63. A full
parent host application link was also attempted, but failed on missing
`kDispatchTable` symbols because that host configuration did not supply a
Dreamcast empty dispatch table; it is not counted as a passing SMA2 host build.
The current nullDC3 outcome is not a regression verdict for the game runtime:
the test stops before SMA2 startup, and no equivalent physical hardware test
was authorized.
