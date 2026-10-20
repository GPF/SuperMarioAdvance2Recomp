# SMA2 native PVR mixed-row fixture — 2026-10-19

## Purpose

This default-OFF diagnostic uses the existing SMA2 frame-201 checkpoint,
shadow-mGBA bridge, software oracle, `PVRGBAFrame()` lifecycle, and raw PVR
front-buffer capture. It forces one 8-row software-owned band while keeping
the remaining 152 rows native-owned. It does not change production defaults
or the mGBADC renderer.

The bridge selects the first 8-row span clear of every active OAM object's
vertical bounds. It checks the same span against final OAM before submission.
This avoids mGBADC's documented whole-OBJ suppression behavior and isolates
the SMA2 fallback-buffer ownership path. The bridge still copies the complete
latched software framebuffer into mGBA's fallback buffer before
`PVRGBAFrame()`; mGBADC uploads the marked fallback rows from that buffer.

The fixture accepts a capture only when one complete frame has 160 seen rows,
152 bridge-native rows, 8 fallback rows, 160 captured/displayed counters, and
8 backend fallback rows. The preserved capture path waits for render
completion, checks stable displayed-page ownership, and compares all 38,400
visible RGB565 pixels against the same frame's latched software oracle.

## Host and build validation

- Existing SMA2 bridge contract tests pass with the 160-row mixed case and
  OBJ-free band selection.
- The test compiled directly with host GCC 13 using `-Wall -Wextra -Werror`.
- Dreamcast build used KOS 2.3.0 and SH-4 GCC 17.0.0 from
  `/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc`; the same shell sourced
  `/opt/toolchains/dc/kos/environ.sh` and verified `command -v sh-elf-gcc`.
- Configuration: native BG/OBJ ON, software composition ON, front capture ON
  for one frame, mixed-row fixture ON, global force fallback OFF, checkpoint
  autoload ON, AOT/dynarec OFF.
- The ELF is `SuperMarioWorldRecomp_mixed_rows.elf`, SHA-256
  `cf5fbacbb005e49366a91cf9961b9c6990f5a971d121539580cb911b3f5c7ac7`.
- Renderer source is the source-pinned SMA2 copy, SHA-256
  `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`.
- ROM SHA-256: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`.
- BIOS SHA-256: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`.
- Frame-200 checkpoint SHA-256:
  `cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188`.

`bridge_contract_tests.log`, `cross_build.log`, and the ELF are retained here.
The CMake configure for the host contract target attempted a GBARecomp
FetchContent download and could not resolve GitHub; the isolated direct host
compile and the Dreamcast cross-build both completed.

## Hardware acceptance status

**PASS — mixed-row ownership and pixel gate.** The first authorized launch
uploaded the ELF but exited before restore because its mounted `/pc` payload
was missing the configured ROM and BIOS. It produced no render evidence and
did not retry. After the user authorized another attempt, the payload was
corrected and hashes were checked in the network-enabled KOS shell; one locked
Dreamcast launch then restored checkpoint frame 200 and passed frame 201.

Frame 201 reported 160/160 rows accounted, 152 native and 8 software fallback
rows (rows 0–7), 160 captured/displayed rows, and 8 backend fallback rows.
`PVRGBAFrame()` counter validation passed. PVR ready/render waits both returned
0 (success); VBlank occurred; front/back pages were distinct and unchanged
across capture. Actual displayed-front-buffer output matched the same-run
software oracle at all 38,400 visible RGB565 pixels: 0 mismatches, first
mismatch `(-1,-1)`. Independent host `cmp` also passed on the two 76,800-byte
captures. Both captures hash to
`0ac1575875ad5bb2a4c95e82d949fcbea28f7e8c6e395b565811ff14929bcf6a`.
The state fingerprint is `12f36c915005eac5`; software RGB888 and RGB565
fingerprints are `e6b64e95140b714f` and `0d585ef913d00d74`. See
`serial_retry_full.log`, `preflight_retry.txt`, `retry_acceptance.txt`, and the
retained raw capture files. The lock was released and no additional launch was
made.

This is a mixed-ownership boundary gate for this restored frame and scene. It
does not reopen or extend mGBADC's renderer effects matrix and provides no
performance result.
This is only a mixed-ownership boundary gate. It does not reopen or extend
mGBADC's renderer effects matrix and provides no performance result.
