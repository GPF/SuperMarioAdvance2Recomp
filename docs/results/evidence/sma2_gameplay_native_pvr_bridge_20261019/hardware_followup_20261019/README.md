# SMA2 frame-201 PVR acceptance — 2026-10-19

## Verdict

**PASS for the tested frame-201 scene.** The all-fallback presentation gate passed first. The second run used 160 native BG/OBJ rows, zero fallback rows, and matched the same-run software oracle exactly. This establishes correctness for this restored state/frame only; it does not establish correctness for every SMA2 effect combination or performance improvement.

## Frozen inputs and sources

Both launches used identical inputs:

- ROM SHA-256: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`
- BIOS SHA-256: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`
- Frame-200 checkpoint SHA-256: `cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188`
- mGBADC `dc-pvr-gba.c` SHA-256: `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`
- SMA2 bridge source SHA-256: `61e11f5c316887802c3afd61dd0bb280656433b15a8442437913012b9812fb54`

Full source hashes are in `../offline_followup_20261019/SOURCE_SHA256SUMS`; per-run preflight records are alongside each run.

## Launch and ownership evidence

Each ELF was hash-checked immediately before launch. Both launches sourced `/opt/toolchains/dc/kos/environ.sh` in the same network-enabled shell, verified `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc` version 17.0.0, confirmed UDP socket availability, and used the shared launcher `/home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh` against `192.168.0.128`. Each preflight found the lock free and no `kos-tool` process. Both lock sessions exited and released the lock; no loader remains active.

The first shell preflight was stopped by `set -u` inside KOS `environ.sh` before lock acquisition or console contact. The corrected launch used `set -e` without nounset while sourcing KOS, then performed all checks and launched once per ELF. This was not a hardware retry.

## Results

Both runs restored `frame=200` and produced the identified complete resumed frame 201. The software oracle fingerprints matched the previous same-run reference: RGB888 FNV-1a `e6b64e95140b714f`, RGB565 FNV-1a `0d585ef913d00d74`, state FNV-1a `12f36c915005eac5`.

| Gate | Force fallback | Native eligible |
|---|---:|---:|
| ELF SHA-256 | `21ef499c6ca344b0533680fa9b270ef15bae9e76b7e28eaa5c90e2abdd396474` | `ce326fbf7f82e27c743985c90f1e038b27a50fad55f84c2b0aca239b55702103` |
| Scanlines | 160 | 160 |
| Native rows | 0 | 160 |
| Fallback rows | 160 (`0x40` diagnostic-force) | 0 |
| Captured / displayed | 160 / 160 | 160 / 160 |
| Pixel mismatches | 0 / 38,400 | 0 / 38,400 |
| First mismatch | none | none |
| Software vs PVR RGB565 SHA-256 | identical `0ac1575875ad5bb2a4c95e82d949fcbea28f7e8c6e395b565811ff14929bcf6a` | identical `0ac1575875ad5bb2a4c95e82d949fcbea28f7e8c6e395b565811ff14929bcf6a` |
| Hardware exit | 0 | 0 |

Independent `cmp` confirmed byte-for-byte equality of each run's 76,800-byte software and PVR capture files. The capture line reports `ready=0`, `render_done=0` (successful KOS wait return codes), `vblank=1`, front page `0xa511f000`, back page `0xa591f000`, and unchanged front/back identities after copying.

For the force-fallback run, the capture completed in 48,325 us; upload/submission were 3,828/279 us. For native-eligible, capture completed in 37,863 us; upload/submission were 441/778 us. These correctness-run timings are diagnostic only and are not performance conclusions.

## Interpretation

The force-fallback run confirms that disabling RGB565 PVR dithering makes the all-fallback path reproduce this software oracle exactly. The native-eligible run confirms that the exact frame-201 state formerly classified as all-fallback can render through the existing native BG/OBJ backend, and its displayed PVR output also matches the software oracle exactly. The window/effect/mosaic predicates were conservative for this state, as diagnosed offline.

Complete serial logs, frozen ELFs, preflight records, raw captures, exit statuses, and checksums are preserved in this directory. No further launch was made.
