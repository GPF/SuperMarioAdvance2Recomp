# SMA2 native PVR offline follow-up — 2026-10-19

The ELFs were first prepared offline. The separately authorized paired hardware
acceptance was later run once per ELF; results and raw captures are in
`../hardware_followup_20261019/README.md`.

## ELF variants

Both ELFs were built with KOS environment sourced and SH-4 GCC 17.0.0, native BG/OBJ PVR enabled, front capture enabled, checkpoint autoload enabled, and software composition retained.

- `SuperMarioWorldRecomp_force_fallback.elf`: `SMA2_NATIVE_PVR_FORCE_FALLBACK=ON`. Intended for the first future hardware gate: all rows use the software fallback texture path, so the dither-off output can be compared against the software oracle.
- `SuperMarioWorldRecomp_native_eligible.elf`: force-fallback OFF. Uses the effective-state classifier; frame 201 should exercise native BG/OBJ rendering if the live per-line state matches the checkpoint state.

The force-fallback switch is diagnostic, default OFF, and reports reason bit `0x40` as `diagnostic_force`.

## Inputs and source provenance

Frozen input hashes from `front_capture_attempt_20261019/prelaunch_sha256.txt`:

- ROM: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`
- BIOS: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`
- Checkpoint: `cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188`
- mGBADC renderer source: `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`
- SMA2 bridge source used for this build: `61e11f5c316887802c3afd61dd0bb280656433b15a8442437913012b9812fb54`

The exact BIOS value and all original run inputs are preserved in the referenced prelaunch manifest.

## SHA-256

See `SHA256SUMS` for both ELF hashes. The build used `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc` and `-dumpversion` reported `17.0.0`.

## Acceptance order

The acceptance order was force-fallback first, then native-eligible only after
the all-fallback pixel gate passed. Both stages used the existing capture
protocol, render-completion wait, page-ownership checks, exclusive lock, and
network-enabled KOS launch shell. See the hardware report for results.
