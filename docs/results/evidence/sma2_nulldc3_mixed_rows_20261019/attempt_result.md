# nullDC3 mixed-row comparator attempt — 2026-10-19

Status: **INCONCLUSIVE — bounded run ended before checkpoint restore.**

Exactly one emulator launch was made, using the strip-debug copy of the existing mixed-row validation ELF and the fresh staged `/pc` root. The emulator accepted the ELF, booted KOS/SMA2, and opened the staged BIOS and ROM. Its 24-VBlank bound expired before `CHECKPOINT_OK restored frame=200`, before guest frame 201, and before any native PVR capture or row-ownership record. No software/PVR RGB565 captures were produced, so the ownership-aware comparator was not run. There was no second launch.

The process returned 0 because nullDC3 reached its configured VBlank bound. This is not a guest validation pass. The original mixed-row fixture's Dreamcast evidence remains valid and untouched; this attempt adds no emulator correctness evidence.

Preflight had passed: the persistent nullDC3 hash, ELF and staged game.toml/ROM/BIOS/checkpoint hashes matched the manifest; original ELF was 19,148,148 bytes and the stripped copy 2,161,936 bytes; both loadable-image hashes were `5fdd1215c40725c2313ec34089f089da38255cad7f1c4979849ac0e8bf6f675b`. The stripped ELF retained the mixed-fixture capture and row-class strings.

Exact command: `launch_command.txt`. Full serial output: `serial.log`. Input/ELF hashes: `prelaunch_sha256.txt`. Output hashes: `run_artifact_sha256.txt`.

A later attempt would need a longer emulator VBlank bound (the established frame-201 nullDC3 run used 120) and separate authorization. Do not infer that the fixture or comparator failed; this attempt never reached the compared frame.

## Later authorized attempt

A subsequent user instruction authorized another bounded run. It used a fresh `retry120/` portable and `/pc` root, the same preflight-verified stripped ELF and assets, and a 120-VBlank limit. This run restored the checkpoint and completed frame 201. See `retry120/` and `docs/results/SMA2_NULLDC3_MIXED_ROW_COMPARATOR_20261019.md`; the first run's log and results above remain unchanged.
