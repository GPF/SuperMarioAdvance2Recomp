# SMA2 Dreamcast Port Plan

This is a research roadmap, not implementation authorization. SMA2 owns title configuration, startup, generated game integration, and title-specific validation. Reusable ARM/Thumb analysis, SH-4 code generation, GBA runtime services, and KOS platform support belong in GBARecomp.

## 1. Repository boundary and host baseline — COMPLETE

The observed PC runtime log is documented in [PC_BASELINE.md](PC_BASELINE.md). It identifies the host as Ubuntu/Linux; “Dinput For Windows” is the controller label. The run establishes only the behavior reported for that host session.

## 2. One-function KOS static-link probe — COMPLETE (isolated fixture)

The first probe generated a 296-byte SH-4 function and statically linked a KOS ELF. The generated object had 11 relocations, all `R_SH_DIR32`; the final ELF had no unresolved symbols or dynamic loader. This verifies one generated function and an explicit harness ABI path only. It does not run on hardware or establish that the game/runtime is buildable. See `/home/gpf/code/gbarecomp/docs/handoff/sma2/FIRST_PROBE_RESULT.md` and [build notes](BUILDING_DREAMCAST.md).

The probe source checkout was GBARecomp revision `5e89943528aee7bc1c399bdbd035364a4b41dd10`. SMA2's submodule remains pinned to `647e2ab8c69666a55d04d82437002f1f1772c8bf`; it is not initialized here. Do not infer compatibility between these revisions.

## 3. Two-function call/return and dispatch probe — COMPLETE (physical behavioral gate)

The two-function ARMv4T fixture (`BL` + `BX LR` + generated dispatch-table linkage) was
run on a physical Dreamcast on 2026-10-15. Both generated functions entered once through
the emitted table/call path. The trace showed BL's `0x080000CC` return frame, Function B's
BX LR return to A, A's final BX LR to sentinel `0x08000140`, and a balanced harness
return stack. All 16 guest registers and full CPSR matched expectation; `CPSR.T=0`, there
were no dispatch misses, the tick budget was not exceeded, and dcload reported
`Program returned 0`. The report, full console capture, ELF checksum, link map, and lock
owner record are in [the hardware report](results/SH4_TWO_FUNCTION_HARDWARE.md).

This closes only the isolated generated-code behavioral gate. The harness supplies a
small explicit dispatch/call-return policy and does not include the GBARecomp runtime,
SMA2, SDL, gameplay, or peripheral services. It is not a Dreamcast port claim. The
separate SMA2 submodule remains pinned to `647e2ab8c69666a55d04d82437002f1f1772c8bf`;
the generated code and headers used for this experiment came from GBARecomp
`5e89943528aee7bc1c399bdbd035364a4b41dd10`.

## 4. ARM/Thumb correctness and state contract

Document CPU-state ownership, PC/flags behavior, ARM/Thumb exchange, calls/returns, and unsupported exits. Any differential fixtures must compare against the reference interpreter and expose indirect exits explicitly.

## 5. GBA services and platform split

Establish explicit contracts for bus, timing, DMA, IRQ, PPU, audio, input, saves, BIOS, and title integration. Reusable KOS adapters belong in GBARecomp; title policy stays here.

## 6. Dynamic code and coverage

Inventory copied/mutable code and indirect control flow. Define visible native and fallback reporting plus invalidation requirements before integration.

Gate D1's original link and ELF SHA-256 were reproduced using the original scratch objects and
output/map paths: 1,022,124 B text, 5,804 B data, 351,116 B bss, zero undefined symbols, hash
`26e99822ab7af551975bb41110786f245f6834ec531d4ab28815b2f1b50f9db5`. Unmodified fresh-source
compilation still fails on SH-4 layout and `std::min` issues; scratch-only fixes enabled the
bus-bound D3 build. Gate D2 host validation is **PASS**: all five corrected fixtures match an
independently configured interpreter-only reference on CPU state, cycles, and byte-identical
per-instruction traces. Gate D3 then ran all five fixtures twice on physical Dreamcast hardware.
The bounded-ring trace ELF matched all 54 per-instruction fingerprints per run against the
host interpreter-only reference; both hardware captures were byte-identical. Architectural
state, CPSR/T, PC/LR, cycles, writable memory, terminal state, call depth, and observed
native/interpreter transitions also matched. Verdict: **GO for this bounded fixture gate**, not
a full-game port claim. See the [D3 hardware report](results/SH4_GATE_D3_HARDWARE.md) for
provenance, hashes, console logs, and memory measurements, and the
[D2 fixture report](results/SH4_GATE_D2_TRANSITIONS.md).

## 7. Full-game Dreamcast validation

The isolated two-function behavioral gate is complete; full SMA2 hardware validation remains open. Only after a runnable title artifact exists, record its exact executable/map, toolchain, loader/boot method, RAM use, ROM residency, cache behavior, input/display/audio/save results, and repeatable gameplay. The fixture result does not establish game support. PC and emulator runs do not establish hardware support.

## 8. Performance and code-size decision

Use measured SH-4 artifacts and hardware results for a GO decision. Record native coverage, startup costs, code footprint, peak RAM, and frame/runtime results. Do not extrapolate SH-4 size from host binaries or the one-function KOS link floor.

The valid KOS-flagged all-64-shard SH-4 compile measured 38,343,560 bytes (36.57 MiB)
of generated `.text`. Adding the full dispatch table, resident ROM, and KOS link floor
gave a 44.20 MiB compile-stage estimate. The subsequent B6 link measured 47,159,468 bytes
(44.97 MiB) of PT_LOAD memory, so the full static corpus is a NO-GO for 16 MiB retail main
RAM. Peak RAM is still unmeasured. A reduced corpus plus interpreter fallback remains unmeasured and may change
both code and dispatch-table sizes. The earlier compile without KOS target flags is not
a valid SH-4 measurement. See [CPP port feasibility](DREAMCAST_CPP_PORT_FEASIBILITY.md).
The first reduced-corpus size measurement was **SIZE-ONLY**: its two-function reset-root
closure measured 1,468 B generated `.text` and 160 B table data, without a linked
production bridge/interpreter. Gate D3 subsequently cleared the bounded linkability issue
using an isolated source archive, C99 namespace shim, exceptions, and two scratch-only
portability corrections; see the [D3 hardware report](results/SH4_GATE_D3_HARDWARE.md).
The reduced two-function estimate and five-fixture D3 ELF are distinct selections and
must not be conflated. Neither establishes full-game fit. The original size-only details
remain in [reduced corpus measurement](results/SH4_REDUCED_CORPUS_MEASUREMENT.md).
