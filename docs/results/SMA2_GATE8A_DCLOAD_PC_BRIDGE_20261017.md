# Gate 8A - nullDC3 dcload-compatible /pc bridge (offline only)

Verdict: **CONDITIONAL** (implemented, host-tested and statically verified; no guest has been run, REIOS booting a KOS ELF is unverified).

## 1. dcload/KOS ABI (sources)
Detection word 0x8C004004==0xDEADBEEF (KOS fs_dcload.c:421-429, startup.S:143-149; kos-tool client/dreamcast/crt0.S, target.c:115). Vector at 0x8C004008 (dcload_syscalls.c). cmd r4, p1-p3 r5-r7, result r0 (confirmed by disassembling dcload_syscall_native in the frozen B16 ELF at 0x8c082950). assign_wrkmem(14) returns -1 = dcload-ip. Open flags are newlib values; stat is 60-byte LE. Details: nulldc3 docs/nulldc3/dcload-bridge.md.

## 2. SMA2 filesystem inventory (frozen ELFs use only /pc)
Reads: /pc/game.toml, gbarecomp/bios/gba_bios.bin (16384 B), roms/super_mario_world_usa.gba (4194304 B, one request), sma2_checkpoint.bin (635929 B, mandatory), saves/*.sav, optional sma2_pmu.txt / sma2_exit.txt. Writes: sma2_h5_immediate.bin, sma2_h5_endpoint.bin via tmp + rename (link+unlink). stat for exists/is_regular_file. Console on fd 1. No directory enumeration or mkdir.

## 3. Architecture
REIOS HLE trap at 0x8C004010; words installed in reios_boot after its RAM wipe; service layer (dcload_abi) over a sandboxed HostFs (dcload_fs, openat2 RESOLVE_BENEATH|NO_SYMLINKS, root fd, regular files only, 32 fds, 64 KiB chunks). Config: `config:Dcload.PcRoot` (empty = disabled, writes nothing), `config:Dcload.PcWritable`. Recorder and debugger RPCs untouched.

## 4. Changed files (/tmp/nulldc3-sh4trace, uncommitted)
New: libswirl/reios/dcload_{fs,abi,bridge}.{h,cpp}, tests/dcload/ (CMakeLists, dcload_test.cpp, kos_fixture/{dcload_fixture.c,make_fixture_dir.py,check_output.py,static_check.py,build.sh}), docs/nulldc3/dcload-bridge.md. Modified: libswirl/reios/reios.cpp (include, register_hook, install/reset/shutdown calls), tests/CMakeLists.txt (add_subdirectory(dcload)). Earlier-gate recorder files unchanged this gate. Original checkout untouched.

## 5. Host tests
dcload.host: ~260 checks pass. Negative controls caught: no NO_SYMLINKS (17 failures), no `..` check (4), ignored writable flag (4); good source restored.

## 6. KOS fixture
Built with kos-cc: dcload_fixture.elf sha256 3dd604623f8f94c46a80476b0cc66c86c201660f5881447089d6db8ad436b23d (2,100,152 B), entry/loads 0x8C010000-0x8C0CB064. static_check.py: all checks pass (magic/vector constants, fs_dcload symbols, no SMA2 strings, generator parity). check_output.py verified against synthetic good (rc 0) and bad (rc 1) logs. Fixture dir hashes: evidence dir fixture_dir.sha256. NOT executed.

## 7. Regression
Host-only ctest (156 tests): 77 passed, 0 failed, 79 skipped (external data absent: vector submodules, 2ndmix/hello ELFs, devtools, wren7). 128 of 284 tests boot a guest and were NOT run (Gate 8A forbids). Skipped and not-run are not passes. Disabled-by-default is proven only at unit level.

## 8. Risks
REIOS ELF boot of a KOS binary unverified; real-BIOS+CDI boot has no install hook; ~636 KB H5 writes and rename semantics only host-tested; dcload edge semantics (naive host path in real dcload vs sandbox here) differ deliberately; 32 fd cap.

## 9. First live test (needs approval)
Run dcload_fixture.elf under nullDC3 REIOS ELF boot with `config:Dcload.PcRoot=.../pcroot_w`, `Dcload.PcWritable=yes`, capture console, check_output.py --writable. Then: confirm /pc listing for the gold-hash SMA2 media dir, boot frozen B16 ELF unchanged, confirm frame-200 checkpoint restore, verify B16 measurement-window breakpoint, capture a small bounded SH-4 sample, stop before large capture or L0B16. Recorder 10 M limit is ample for a smoke sample.

## 10. Verdict
CONDITIONAL. Evidence: /tmp/nulldc3-sh4trace-gate8a-20261017/. Stopped; awaiting explicit approval before any guest run.
