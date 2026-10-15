# Building and Testing SMA2 for Dreamcast

This document records the interpreter-first SMA2 Dreamcast build and its physical
bring-up status. The game now boots on a Dreamcast and displays real GBA PPU
output; gameplay and useful performance remain unverified. Historical isolated
SH-4 probe results are retained below as separate evidence.

## Prerequisites

The first KOS probe used the local toolchain under `/opt/toolchains/dc`. In a fresh shell, initialize KOS before invoking `kos-cmake`, `kos-cc`, or `sh-elf-*` tools:

```bash
source /opt/toolchains/dc/kos/environ.sh
```

The recorded environment used KOS 2.3.0, `sh-elf-g++` GCC 17.0.0, and binutils 2.47. Tool versions and local paths can change; capture them for every new experiment. The first probe also used CMake 3.28.3 and host GCC 13.3.0.

GBARecomp is a separate framework dependency. This SMA2 checkout pins submodule commit `647e2ab8c69666a55d04d82437002f1f1772c8bf`, and the submodule is currently uninitialized. Do not initialize/update it or change its pin without explicit authorization. The successful first probe instead used the separate `/home/gpf/code/gbarecomp` checkout at `5e89943528aee7bc1c399bdbd035364a4b41dd10`; see [dependency details](DREAMCAST_DEPENDENCIES.md).

## Verified build: isolated one-function probe

The probe built a 200-byte ARM ROM fixture with `MOV r0,#42; BX LR` at offset `0xC0`, generated the function at `0x080000C0`, compiled it with KOS `sh-elf-g++`, and statically linked one KOS ELF. It did not build the SMA2 game or execute on a Dreamcast.

The full measured command sequence and artifacts are in `/home/gpf/code/gbarecomp/docs/handoff/sma2/FIRST_PROBE_RESULT.md`. Its central steps were:

```bash
mkdir -p /tmp/gba-sh4-aot-probe
cd /tmp/gba-sh4-aot-probe

cmake -S /home/gpf/code/gbarecomp -B host-build \
  -DGBARECOMP_TOMLPP_INCLUDE_DIR=/usr/include/toml++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build host-build --target gba_recompile -j4

./host-build/gba_recompile --rom fixture.gba --entry 0x080000C0 \
  --symbols symbols.tsv --rom-base 0x08000000 --out gen --codegen-shards 2

source /opt/toolchains/dc/kos/environ.sh
# Compile the generated shard and explicit-state harness with sh-elf-g++;
# use the include paths and flags recorded in FIRST_PROBE_RESULT.md.
# Statically link with: sh-elf-g++ $KOS_LDFLAGS obj/harness.o \
#   obj/probe_shard0.o -o probe.elf $KOS_LIBS
```

Do not recreate the fixture by hand and claim an equivalent result without preserving the exact inputs, tools, commands, and output. The report identifies the function as 296 bytes and the static SH-4 ELF as ELF32, little-endian, `EXEC`; its object had 11 `R_SH_DIR32` relocations and its final ELF had no unresolved symbols or dynamic loader. These results describe only that fixture and harness.

## Current interpreter-first game build

Build the KOS ELF with the SMA2-local compatibility overlay:

```bash
tools/build_dreamcast.sh
```

The script sources the KOS environment, stages the overlay in the ignored
`build-dreamcast/gbarecomp-overlay/` directory, configures `SMA2_DREAMCAST=ON`,
and builds `build-dreamcast/SuperMarioWorldRecomp.elf` plus its linker map. The
cart AOT corpus is not linked; the production ARM/Thumb interpreter is forced
on. ROM, BIOS, and `game.toml` are loaded from the `/pc` dcload filesystem paths
documented in [port status](SMA2_DREAMCAST_PORT_STATUS.md). The script does not
modify or initialize the separate GBARecomp checkout.

The current ELF has `sh-elf-size` totals of 1,145,909 B text, 8,176 B data, and
1,145,984 B BSS (2,300,069 B combined). These are linked section sizes, not
runtime peak RAM: ROM residency, guest memories, KOS, thread stack, buffers, and
allocator use still need an on-hardware headroom measurement.

For physical launches use the shared-lock procedure below. The user has directly
observed the Nintendo Presents splash, SMA2 menu, game title screen, and second
startup splash. D-pad navigation and KOS PCM audio have also been observed.
Current startup is very slow; gameplay entry and sustained operation are not yet
confirmed. See [port status](SMA2_DREAMCAST_PORT_STATUS.md) for the current run
log and telemetry limitation.

## Historical isolated SH-4 probes

The one- and two-function `BL`/`BX LR` fixtures passed on hardware; see the
[two-function result](results/SH4_TWO_FUNCTION_HARDWARE.md). Full-corpus and
host coverage evidence is recorded in [B6](results/B6_GATE_LINK.md) and
[B7](results/B7_BASELINE_COVERAGE.md). These corpus results do not describe the
interpreter-first game ELF, which omits cart AOT code.

## Shared physical console lock (required for every loader session)

Use mGBADC's existing [shared launcher](/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh) directly; SMA2 must not create another lock:

```bash
LOCK=/tmp/mgbadc-dreamcast-console.lock
WRAPPER=/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh

# Availability and owner check (informational only; launch still acquires atomically).
if [ -d "$LOCK" ]; then cat "$LOCK/owner"; else echo "lock directory absent"; fi
pgrep -ax kos-tool || true

# Build the probe first, then launch it under the shared lock.
source /opt/toolchains/dc/kos/environ.sh
MGBA_DC_LOCK_TASK=sma2-unique-test-label "$WRAPPER" \
  -t 192.168.0.128 -x /absolute/path/to/test.elf -m /absolute/path/to/host-files
```

The lock directory is `/tmp/mgbadc-dreamcast-console.lock` on this host, shared by
all repositories and independent local processes that use this wrapper (same host `/tmp`;
not a network-wide lock). Atomic
`mkdir` grants one owner; the wrapper records `pid`, `task`, UTC time, and the exact
`kos-tool` arguments in `owner`. A competing wrapper exits with status 3 and prints
the current owner. It also refuses launch if an unwrapped `kos-tool` process is already
running. The lock is advisory for launches that ignore this convention, so never start
`kos-tool` directly.

The wrapper keeps the lock for the entire `kos-tool` session, including upload, program
output capture, and termination. It removes the lock on normal exit and on trapped
HUP/INT/TERM. SIGKILL, power loss, or host failure can leave a stale directory. Do not
remove it with `rm`, do not kill a loader, and do not clear it merely because a session
seems slow. First inspect `owner` and verify no loader is active; only then use the
wrapper's guarded stale recovery:

```bash
owner_pid=$(sed -n 's/^pid=//p' "$LOCK/owner" 2>/dev/null)
[ -z "$owner_pid" ] || ps -p "$owner_pid" -o pid,stat,args
pgrep -ax kos-tool || true
# Only after confirming the recorded wrapper PID and all loaders are gone:
"$WRAPPER" --clear-stale
```

`--clear-stale` itself refuses while any `kos-tool` process exists and removes only the
known shared lock directory. Before using it, also inspect the recorded PID because the
script does not validate that PID or use an OS `flock`; the directory may remain after
SIGKILL/power loss. It is not automatically released by the kernel. If the owner is still
active, let that session end and retry the locked launch; no automatic waiting policy is
installed. See the recorded
[hardware validation](results/SH4_TWO_FUNCTION_HARDWARE.md) for the exact session.

## Testing levels

1. **Host generator/build checks:** run only where the selected GBARecomp checkout and dependencies are already available. Record the exact revision; the uninitialized submodule must not be silently populated.
2. **Static SH-4 inspection:** inspect ELF header, program/section headers, symbols, relocations, undefined symbols, and disassembly with `sh-elf-readelf`, `sh-elf-nm`, and `sh-elf-objdump`. An ELF link is not proof of correct execution.
3. **Emulator checks:** useful as supplementary evidence when a runnable artifact exists; emulator success is not hardware support.
4. **Physical Dreamcast checks:** the interpreter-first game ELF has loaded the
   actual ROM and BIOS, rendered splash/title output, accepted menu input, and
   produced audio on hardware. A title-to-level transition, sustained gameplay,
   and full runtime memory headroom remain unverified. Keep observations and
   logs in [port status](SMA2_DREAMCAST_PORT_STATUS.md).

The interpreter-first KOS build is title-local. Reusable runtime/backend work
still belongs in GBARecomp; compatibility adaptations for this build are staged
through the reversible SMA2 overlay rather than edits to that checkout.
