#!/bin/bash
# build_prod343.sh M|N : tracer harness against LIVE trees. M = 343-function reset corpus + 7 IWRAM entries (arm B-like), N = 343 only (arm A-like)
set -e
V=$1; W=$(pwd); G=/home/gpf/code/gbarecomp; R=/home/gpf/code/SuperMarioAdvance2Recomp
XB=$W/x32p_343; GEN=$W/../prodgen/B
INC="-I$R/src -I$G/src/armv4t -I$G/src/gba -I$G/src/runtime -I$G/src/debug -I$R/src/snapshots -I/usr/include/SDL2 -I$G/external/arm-recomp-core/cmake/../profiles/armv4t_gba -I$G/src/recompile"
FL="-DGBARECOMP_ENABLE_MODS=1 -DGBARECOMP_HAVE_SDL2 -DSMA2_SNAPSHOT_NATIVE_CORPUS=1 $INC -I/tmp/sma2_repro_reset -m32 -malign-double -O2 -std=gnu++20"
O=$W/objp_C; mkdir -p $O
c++ $FL -c iw_main_cls.cpp -o $O/iw_main_cls.o
c++ -m32 -malign-double -O2 -std=gnu++20 $INC -c $R/src/sma2_iwram_aot.cpp -o $O/sma2_iwram_aot.o
LIBS=$XB/gbarecomp_build; OBJ=$XB/CMakeFiles/sma2_snapshot_diff.dir
CORP="$OBJ/tmp/sma2_repro_reset/recompiled_000.cpp.o $OBJ/tmp/sma2_repro_reset/recompiled_001.cpp.o $OBJ/tmp/sma2_repro_reset/recompiled_002.cpp.o $OBJ/tmp/sma2_repro_reset/recompiled_003.cpp.o $OBJ/tmp/sma2_repro_reset/symbol_map.cpp.o"
if [ $V = M ]; then
  for f in $GEN/recompiled_00*.cpp; do c++ -m32 -malign-double -O3 -DNDEBUG -std=gnu++20 $INC -I$GEN -c $f -o $O/iwram_$(basename $f .cpp).o; done
  c++ -m32 -malign-double -O2 -std=gnu++20 -c merged_dispatch_prod.cpp -o $O/merged_dispatch.o
  EXTRA="$O/iwram_recompiled_000.o $O/iwram_recompiled_001.o $O/merged_dispatch.o"
else EXTRA="$OBJ/tmp/sma2_repro_reset/dispatch_table.cpp.o"; fi
WRAP="-Wl,--wrap=runtime_should_yield,--wrap=runtime_irq,--wrap=runtime_tick,--wrap=runtime_dispatch,--wrap=runtime_dispatch_miss,--wrap=runtime_exception_return,--wrap=runtime_idle_backedge,--wrap=runtime_swi,--wrap=runtime_insn_fp,--wrap=runtime_bridge_interpret,--wrap=runtime_force_interp_step"
LL="-Wl,--start-group $LIBS/libgbarecomp_runtime.a $LIBS/libgbarecomp_debug.a $LIBS/libgbarecomp_gba.a $LIBS/libgbarecomp_armv4t.a -Wl,--end-group $LIBS/libgbarecomp_recompile_core.a $LIBS/libgbarecomp_heal_gate.a $LIBS/libgbarecomp_gba.a $LIBS/libgbarecomp_armv4t.a /usr/lib/i386-linux-gnu/libSDL2.so"
c++ -m32 -malign-double $WRAP $O/iw_main_cls.o $O/sma2_iwram_aot.o $OBJ/src/snapshots/sma2_checkpoint.cpp.o $CORP $EXTRA -o iwp_C $LL
ls -la iwp_C
