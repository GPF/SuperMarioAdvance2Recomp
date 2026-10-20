# SMA2 dynarec nullDC3 startup crash — PT_TLS postmortem (2026-10-20)

Emulator evidence only; no Dreamcast hardware was launched.

## Symptom
The `SMA2_SH4_DYNAREC=ON` ELF (SHA-256 `24097243…bea17`, stripped; unstripped
`2ad5d355…062c`) faulted in `___do_global_ctors_aux` at PC `0x00000002` before
SMA2 `main()`. The pre-merge control `ce17c68f…54c4c2d` ran to frame 2401.

## Do not "fix" the linker
**`.tbss` sharing address `0x8c197d4c` with `.ctors` is legal.** `.tbss` is a
NOBITS TLS template (a `PT_TLS` header with filesz 0, memsz 0x40); it occupies no
space in the loaded image and the linker is allowed to overlay it on the
following section. The linked ELF was correct: `.ctors` on disk held `-1`, ten
`__GLOBAL__sub_I_*` pointers and a `0` terminator, and the walker's start
address (`0x8c197d38 + 60`) was right. Moving sections or editing the linker
script is the wrong response.

**The defect was the emulator's loader.** nullDC3 `libswirl/reios/reios_elf.cpp`
(lines 44-71) iterates every program header regardless of `p_type`: it copies
`filesz` bytes and then `memset`s `memsz - filesz`. For `PT_TLS` that zero-fills
0x40 bytes at `0x8c197d4c`, over the `.ctors` that `PT_LOAD` had just copied. The
walker then read 0 and called address 0. (Inference: dcload/KOS on hardware
honour `p_type`; this was not tested on hardware.)

## Where the TLS came from
GBARecomp's merged runtime added `thread_local` objects
(`src/armv4t/runtime_arm.h:216-220` `GBARECOMP_TAIL_ARG_TLS`,
`runtime_arm.cpp:855-856,974`, `gba_bus.cpp:643`). 4+4+DispatchEntry+64-byte
scratch accounts for the PT_TLS block.

## Workaround
`apply_no_tls()` in `tools/prepare_dreamcast_framework.py` empties
`GBARECOMP_TAIL_ARG_TLS` and de-qualifies `scratch` in the staged overlay only;
GBARecomp itself is unchanged. Result ELF `41b2051e…bb1faab3`, no `PT_TLS`.

**Caveat — revisit if threading changes.** Dropping `thread_local` is valid only
while the guest runtime state (tail-call hand-off argument, native RAM base,
RAM-entry record, bus scratch) is touched by a single thread. The header comment
justifies the TLS by a TCP debug server/worker threads on host builds; the
Dreamcast runner has none. If a second thread ever enters the runtime on
Dreamcast, this workaround must be replaced (for example by fixing the loader or
by per-thread state), not kept.

## Second defect: asset staging
The runner staged assets as symlinks; nullDC3's `/pc` opens with
`RESOLVE_NO_SYMLINKS` (`dcload_fs.cpp:135`), so all assets were "missing" and an
uncaught `std::filesystem_error` aborted after `main()`. The runner now stages
copies (sources are still hash-checked). Do not mistake this abort for the
constructor fault.

## Result
Run `/tmp/sma2-dynarec-nulldc3-tlsfix-20261020-run2`: `main()` reached, dynarec
backend init, stages 1/2/3/D/E PASS (1 native block each, 0 fallback, 0
mismatches), verdict PASS, emulator status 0. Evidence:
`docs/results/evidence/sma2_nulldc3_dynarec_tlsfix_20261020/` (`console.txt.log`
force-added despite the `*.log` ignore rule, by explicit decision). The 343-entry
AOT baseline was not touched.
