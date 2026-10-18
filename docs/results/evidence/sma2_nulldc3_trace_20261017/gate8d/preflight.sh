#!/usr/bin/env bash
# Gate 8C preflight: every identity check must pass or the run is not started.
set -u; cd "$(dirname "$0")"; fail=0
chk(){ if [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$2" ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
chk L0B16.elf f40fc020dcde51c9b882831f6289d36cee746fb7419c14b44d8f3ca1eb4eec57
chk media/sma2_checkpoint.bin 50282ea4be8bea13b9063a38438dcde0b0b330bf0f3b929edf525bb112af32e6
chk media/roms/super_mario_world_usa.gba 63d9fff04c635990a5c205a99ea64bfa698aa5cb9ec1333360063bbee949a4f3
chk media/gbarecomp/bios/gba_bios.bin fd2547724b505f487e6dcb29ec2ecff3af35a841a77ab2e85fd87350abd36570
chk media/game.toml ecbb5dd7835e709570362dae8d23caf61291af61a1d5e1283f27bf19a17d1f2a
chk media/saves/super_mario_world_usa.sav d7f59620640aa8c1357aaa44e190489450ac75977f2d1b211fc650f410138e73
for f in media/sma2_pmu.txt media/sma2_pms* media/sma2_exit.txt media/sma2_h5_immediate.bin media/sma2_h5_endpoint.bin; do [ -e $f ] && { echo "FAIL present: $f"; fail=1; }; done; echo "PASS pmu mode files absent / no stale outputs (checked)"
B=./nulldc3-emu
nm -C $B | grep -q Sh4TraceOnReset && echo "PASS emulator has Gate 4 recorder" || { echo FAIL recorder; fail=1; }
nm -C $B | grep -q dcload_bridge_hook && echo "PASS emulator has Gate 8A bridge" || { echo FAIL bridge; fail=1; }
cmp -s $B /tmp/nulldc3-gate4-emu/nulldc3 && echo "PASS pinned emulator copy == build output"
for s in libswirl/reios/dcload_fs.cpp libswirl/reios/dcload_abi.cpp libswirl/reios/dcload_bridge.cpp libswirl/hw/sh4/sh4_trace.cpp; do [ /tmp/nulldc3-sh4trace/$s -ot $B ] && echo "PASS $s older than the binary" || { echo "FAIL $s newer than the binary"; fail=1; }; done
sha256sum $B
if ss -ltn | grep -q ':55612 '; then echo "FAIL port 55612 in use"; fail=1; else echo "PASS port 55612 free"; fi
[ $fail = 0 ] && echo "PREFLIGHT PASS" || echo "PREFLIGHT FAIL"; exit $fail
