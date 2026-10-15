# SH-4 Two-Function Probe — Physical Dreamcast Result

**Verdict: GO for the isolated two-function generated-code behavioral gate.** This is not
a GO for the GBARecomp runtime, SMA2 integration, or a complete Dreamcast port.

## Result

The KOS harness ran twice successfully on the Dreamcast. Both runs used the same ELF
SHA-256 `0fd78fcafac824e0d7fb75e5e7cd2c0e5847fa0d0eaf24c11b25fdc91a54f0e4`; each ended
with `GUEST-STATE CHECK: PASS` and `Program returned 0`. The final full console capture is
`/tmp/gba-sh4-aot-probe/probe2_hardware/hardware_console_final.log` and the final status
is `launch_status_final.txt` in that directory.

The 512-byte guest fixture was embedded in the KOS executable and checked on-console:
FNV-1a-32 `0xeb8ac1cf` (fixture SHA-256
`2c7448889537a5ab0ab3367868ca255ca04569247201805a35b4f5b12281ea68`). Function A is at
`0x080000C0`; Function B is at `0x08000100`. The generated C++ files and dispatch table
are byte-identical to the preserved originals in `/tmp/gba-sh4-aot-probe/probe2/`.

Console trace from the passing run:

```text
FIXTURE CHECK: PASS (512-byte embedded fixture; A=0x080000C0 B=0x08000100)
TRACE dispatch-start pc=0x080000C0 cpsr=0x00000000
TRACE dispatch-table-hit pc=0x080000c0 thumb=0
TRACE generated-entry pc=0x080000c0
TRACE guest-BL-push return=0x080000cc depth=1
TRACE generated-entry pc=0x08000100
TRACE guest-BX-LR-return pc=0x080000cc depth=0
TRACE guest-BX-LR-no-frame pc=0x08000140
TRACE sentinel-return pc=0x08000140
RESULT exit_pc=0x08000140 dispatch_hits=1 dispatch_misses=0 entries_A=1 entries_B=1 ticks=9 return_depth=0
GUEST-STATE CHECK: PASS
Program returned 0
```

All 16 registers were compared, not only PC:

| State | Expected | Actual |
|---|---:|---:|
| R0 | `0x00000000` | `0x00000000` |
| R1 | `0x00000011` | `0x00000011` |
| R2 | `0x00000022` | `0x00000022` |
| R3 | `0x00000033` | `0x00000033` |
| R4–R11 | all `0x00000000` | all `0x00000000` |
| R12 | `0x08000140` | `0x08000140` |
| R13 | `0x00000000` | `0x00000000` |
| R14 | `0x08000140` | `0x08000140` |
| R15 | `0x08000140` | `0x08000140` |
| CPSR / CPSR.T | `0x00000000` / `0` | `0x00000000` / `0` |

A generated-entry hook observed A and B exactly once. The generated dispatch table hit
A once with no miss; its linked table words point to `_gf_start_vector` at `0x8c0104e0`
and `_gf_probe_func_b` at `0x8c010410`. The link map contains `.rodata.kDispatchTable` and
both generated function sections. The harness's call-return depth returned to zero and
its 64-tick bound was not reached (9 ticks observed).

## Build and provenance

Build directory: `/tmp/gba-sh4-aot-probe/probe2_hardware/`. It copies the preserved
fixture and generated sources from `probe2/`; it does not alter `probe2.elf` or any
GBARecomp checkout. Generated sources/header hashes were checked against the preserved
files. GBARecomp checkout for the generated code and ABI headers:
`5e89943528aee7bc1c399bdbd035364a4b41dd10`. This differs from SMA2's uninitialized
submodule pin `647e2ab8c69666a55d04d82437002f1f1772c8bf`.

KOS version 2.3.0 (`cf190e23` on the running console), KOS subarch `pristine`,
`sh-elf-g++` GCC 17.0.0 (20260703), GNU Binutils 2.47.20260726. The loader is `kos-tool`
/`kostool` 3.1.0 build `1f5da42`, using dc-load-ip 3.0.1. The ELF is 32-bit little-endian
SuperH, EXEC, flags `0xc (sh4a)`, entry `0x8c010000`, zero undefined symbols; KOS startup
and the installed linker script supplied the entry/startup/link policy.

The build uses `/opt/toolchains/dc/kos/Makefile.rules`, `kos-c++` for C++ compilation,
and `kos-cc` for linking. Reproduce from this directory after sourcing the KOS
environment:

```bash
source /opt/toolchains/dc/kos/environ.sh
rm -f main.o gen2/recompiled_000.o gen2/recompiled_001.o gen2/dispatch_table.o \
  probe2_hardware.elf probe2_hardware.map
KOS_WRAPPERS_VERBOSE=1 make --trace -j1
```

`build_final.log` records each fully expanded compiler invocation and the exact link
command. The link command emitted by the supported wrapper was:

```text
kos-cc -Wl,-Map=probe2_hardware.map -o probe2_hardware.elf main.o gen2/recompiled_000.o gen2/recompiled_001.o gen2/dispatch_table.o
```

The wrapper expanded that to `sh-elf-gcc` with the KOS environment flags, linker script
`/opt/toolchains/dc/kos/utils/ldscripts/shlelf.xc`, KOS library paths, and
`-Wl,--start-group -lkallisti -lm -lc -lgcc -Wl,--end-group`. The exact expanded line is
in `build_final.log`; no hand-written linker script or guessed startup objects were used.
The build succeeded with two generated-code warnings for unused output temporaries
`_co_080000C4` and `_co_080000D0`; no link warnings or unresolved symbols. Final ELF SHA-256
is saved in `elf_sha256.txt`; map, ELF header, program headers, symbols, disassembly,
size, and undefined-symbol inspection are preserved alongside it.

## Hardware lock and configuration

All three launches (one harness-check failure before guest entry, then two passing runs) used the existing mGBADC shared wrapper, not an SMA2-local lock:

```text
/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh
```

The lock is `/tmp/mgbadc-dreamcast-console.lock`; its session is shared by local processes
and repositories using that wrapper. The final launch command was:

```bash
source /opt/toolchains/dc/kos/environ.sh
MGBA_DC_LOCK_TASK=sma2-two-function-hw-20261015-owner-capture \
  /home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh \
  -t 192.168.0.128 \
  -x /tmp/gba-sh4-aot-probe/probe2_hardware/probe2_hardware.elf \
  -m /tmp/gba-sh4-aot-probe/probe2_hardware
```

The final owner snapshot is
`hardware_lock_owner.txt`:

```text
pid=1390706
task=sma2-two-function-hw-20261015-owner-capture
utc=2026-10-15T09:35:02Z
command=-t 192.168.0.128 -x /tmp/gba-sh4-aot-probe/probe2_hardware/probe2_hardware.elf -m /tmp/gba-sh4-aot-probe/probe2_hardware
```

The wrapper held the lock through upload, console output capture, and `Program returned
0`, then removed the lock. Post-run inspection found no lock directory and no `kos-tool`
process. Lock availability/owner inspection, busy handling, and guarded stale recovery
commands are documented in [BUILDING_DREAMCAST.md](../BUILDING_DREAMCAST.md#shared-physical-console-lock-required-for-every-loader-session).

The local target address `192.168.0.128` is the documented mGBADC dc-load-ip target and
was reached by the successful loader session. The harness made no SDL, PVR, audio,
filesystem UI, or gameplay calls; KOS default startup nevertheless logged its 640x480 VGA
mode and Maple device enumeration. dc-load-ip identified a Broadband Adapter
`HIT-0400`. The running KOS reported VGA 640x480; Maple output listed Dreamcast controllers
and VMUs. Exact console board revision was not reported by the loader and remains unknown.
The restricted shell's separate `ip route get` preflight returned `Operation not
permitted`; the authorized loader session succeeded through the network-enabled execution
path. No QEMU fallback or loader investigation was used.

## First attempt and test limits

The first KOS launch stopped at the harness's fixture check before executing generated
functions. The fixture hash matched, but the validator incorrectly treated B's bytes at
ROM offset `0x100` as contiguous with A at `0xC0`. The harness was corrected to check each
function at its own offset. The passing ELF was rebuilt, checksummed, and then launched
twice; both runs passed with the same ELF hash. The first log and its nonzero return are
preserved as `hardware_console.log` and `launch_status.txt`.

The generated guest code performed the real direct generated A-to-B call and return path;
the harness supplied the documented guest dispatch and return-frame hooks. This validates
those two generated functions, table linkage, state reconciliation, and KOS C ABI path in
this fixture. It does not validate the production GBARecomp runtime, interpreter fallback,
SMA2 startup, mutable code, SDL, graphics/audio/input/save behavior, memory fit, or gameplay.
The full 64-shard corpus was not run on hardware. No source, submodule, sibling repository,
commit, or remote was changed.
