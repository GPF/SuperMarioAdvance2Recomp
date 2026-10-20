# SMA2 native PVR consecutive-frame acceptance — 2026-10-19

## Verdict

**PASS for completed gameplay frames 201–204 in the selected restored scene.** One physical Dreamcast launch restored checkpoint frame 200, then captured four complete resumed frames. Each frame used all 160 native BG/OBJ rows, had no software fallback rows, passed render-completion and stable front/back page checks, and matched its same-frame software oracle at every visible RGB565 pixel. This is bounded scene evidence, not general renderer coverage or a performance result.

## Build and inputs

- ELF: `SuperMarioWorldRecomp_multiframe.elf`, SHA-256 `5d79a25a7786da17aee4ef041d5d55db46e12943601ac8b0588d82f3788a1e08`.
- Build: KOS 2.3.0, installed SH-4 GCC 17.0.0; native BG/OBJ ON, software composition ON, front-buffer capture ON, four frames, checkpoint autoload ON, forced fallback OFF, RGB565 sidecar OFF, AOT/dynarec OFF.
- ROM SHA-256: `63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3`.
- BIOS SHA-256: `fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570`.
- Checkpoint SHA-256: `cf2f2a80376525b99943cf48c98b08da45e8c578b2e7bac02b535ab9b8d9f188`.
- mGBADC pinned renderer `dc-pvr-gba.c` SHA-256: `83fd6ab67147a80cda61704f9f1ff06d78cc24d6dbed0e0f932b54e58962f0ef`.
- SMA2 bridge source SHA-256: `9b123c431767de6de697a80c268252853e2e35f338cec65bb1016cf81033754a`.
- `CMakeLists.txt` SHA-256: `3495884877386e615bc1a4d4d5598a04d673152759f0cb9e79a53ae02c20ff0f`; build wrapper SHA-256: `2f2fe2f43137ddd365dd44e45c143c5433624b662f7fad0020768a48ea4b2d85`.

## Hardware result

The launch sourced `/opt/toolchains/dc/kos/environ.sh` in the same network-enabled shell, verified GCC 17.0.0 and a working UDP socket, and used the established exclusive lock and `kos-tool-locked.sh` against `192.168.0.128`. Prelaunch hashes are in `preflight.txt`; complete serial output is in `serial_full.log`. The one launch exited 0; the lock was released and no loader remained.

| GBA frame | State FNV-1a | Software RGB565 FNV-1a | Rows native / fallback | Captured / displayed | PVR page (front / back) | Mismatches |
|---:|---|---|---:|---:|---|---:|
| 201 | `12f36c915005eac5` | `0d585ef913d00d74` | 160 / 0 | 160 / 160 | `0xa511f000` / `0xa591f000` | 0 / 38,400 |
| 202 | `c829ac372d0216b1` | `ae67b801d5be1108` | 160 / 0 | 160 / 160 | `0xa591f000` / `0xa511f000` | 0 / 38,400 |
| 203 | `291ee09969731cb3` | `c846b1dac467ef6b` | 160 / 0 | 160 / 160 | `0xa511f000` / `0xa591f000` | 0 / 38,400 |
| 204 | `c219784c974eaab3` | `4a8efbb030ed6d3a` | 160 / 0 | 160 / 160 | `0xa591f000` / `0xa511f000` | 0 / 38,400 |

All captures reported successful `pvr_wait_ready()` and `pvr_wait_render_done()` return values (`0`), VBlank observed, stable distinct page identities around the copy, and no first mismatch (`-1,-1`). Every software/PVR pair is 76,800 bytes and passes independent `cmp`; the log reports `SMA2_NATIVE_PVR_SEQUENCE result=PASS first=201 last=204 frames=4`. Frame 201 reproduces the earlier known software fingerprints. Frames 202–204 have distinct state and framebuffer hashes, providing deterministic temporal coverage beyond the first resumed frame.

Per-frame software and actual displayed-front-buffer RGB565 data are `sma2_gameplay_frame{201..204}_software.rgb565` and `sma2_gameplay_frame{201..204}_pvr.rgb565`. `CAPTURE_SHA256SUMS` contains their hashes; `SHA256SUMS` covers the ELF, raw captures, preflight, log, exit status, and capture checksum list.

This validates source-buffer-to-displayed-PVR equivalence for four consecutive frames in this scene. All rows stayed native, so mixed native/software fallback ownership was not exercised. No performance conclusion follows from these correctness runs.
