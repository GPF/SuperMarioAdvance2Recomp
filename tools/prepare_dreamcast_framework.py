#!/usr/bin/env python3
"""Stage the pinned local GBARecomp checkout with SMA2-only KOS adaptations."""
from __future__ import annotations

import pathlib
import os
import shutil
import subprocess
import sys
import hashlib


def replace_once(path: pathlib.Path, before: str, after: str) -> None:
    text = path.read_text()
    if text.count(before) != 1:
        raise SystemExit(f"expected one patch site in {path}: {before[:80]!r}")
    path.write_text(text.replace(before, after, 1))


def replace_repeated(path: pathlib.Path, before: str, after: str,
                     expected: int) -> None:
    text = path.read_text()
    count = text.count(before)
    if count != expected:
        raise SystemExit(
            f"expected {expected} patch sites in {path}, found {count}: {before[:80]!r}"
        )
    path.write_text(text.replace(before, after))


def apply_bridge_prof(dest: pathlib.Path, repo_root: pathlib.Path,
                      parity: bool) -> None:
    """Opt-in zone-profiler hooks (SMA2_BRIDGE_PROF); no semantic changes."""
    shutil.copy(repo_root / "src/snapshots/sma2_bridge_prof.h",
                dest / "src/runtime/sma2_bridge_prof.h")
    aborts = dest / "src/runtime/runtime_arm_default_aborts.cpp"
    aborts.write_text('#include "sma2_bridge_prof.h"\n' + aborts.read_text())
    bridge = dest / "src/runtime/runtime_bus_bridge.cpp"
    bridge.write_text('#include "sma2_bridge_prof.h"\n' + bridge.read_text())
    rarm = dest / "src/armv4t/runtime_arm.cpp"
    rarm.write_text('#include "../runtime/sma2_bridge_prof.h"\n' + rarm.read_text())

    replace_once(bridge,
        "static void tick_devices(gba::GbaBus* bus, gba::GbaPpu* ppu, uint32_t cycles) {\n",
        "static void tick_devices(gba::GbaBus* bus, gba::GbaPpu* ppu, uint32_t cycles) {\n"
        "    SMA2_PZ(Z_TICKDEV);\n")
    replace_once(rarm,
        "const DispatchEntry* dispatch_resolve(uint32_t target_pc) {\n",
        "const DispatchEntry* dispatch_resolve(uint32_t target_pc) {\n"
        "    SMA2_PZ_OUTER(Z_DISPATCH);\n")
    # data-access helpers
    fib = [
        ("    uint8_t  read8 (uint32_t a) override { return bus_read_u8(a); }\n", "bus_read_u8(a);"),
        ("    uint16_t read16(uint32_t a) override { return bus_read_u16(a); }\n", "bus_read_u16(a);"),
        ("    uint32_t read32(uint32_t a) override { return bus_read_u32(a); }\n", "bus_read_u32(a);"),
        ("    void write8 (uint32_t a, uint8_t  v) override { bus_write_u8(a, v); }\n", "bus_write_u8(a, v);"),
        ("    void write16(uint32_t a, uint16_t v) override { bus_write_u16(a, v); }\n", "bus_write_u16(a, v);"),
        ("    void write32(uint32_t a, uint32_t v) override { bus_write_u32(a, v); }\n", "bus_write_u32(a, v);"),
    ]
    for line, call in fib:
        pl = (line.replace("read8 (", "read8(").replace("write8 (", "write8(")
                  .replace("uint8_t  ", "uint8_t ") if parity else line)
        shared = parity and pl == line   # identical text in both bus adapters
        replace_repeated(aborts, line, line.replace("{ ", "{ SMA2_PZ(Z_BUS); ", 1),
                         2 if shared else 1)
        if parity and not shared:
            replace_once(aborts, pl, pl.replace("{ ", "{ SMA2_PZ(Z_BUS); ", 1))
    # finer attribution zones
    replace_once(aborts,
        "    const char* strict_env = std::getenv(\"GBARECOMP_STRICT_STATIC\");\n",
        "    SMA2_ZSET(Z_MISS_ENV);\n    const char* strict_env = std::getenv(\"GBARECOMP_STRICT_STATIC\");\n")
    replace_once(aborts,
        "    record_and_log_miss(entry_pc, entry_thumb);\n    if (gbarecomp::overlay_request_compile",
        "    SMA2_ZSET(Z_MISS_REC);\n    record_and_log_miss(entry_pc, entry_thumb);\n    SMA2_ZSET(Z_MISS_OVL);\n    if (gbarecomp::overlay_request_compile")
    replace_once(aborts,
        "    runtime_bridge_interpret(entry_pc, entry_thumb, /*forced_stop_pc=*/0u,\n",
        "    SMA2_ZSET(Z_MISS);\n    runtime_bridge_interpret(entry_pc, entry_thumb, /*forced_stop_pc=*/0u,\n")
    replace_once(aborts,
        "    // ── (1) Stop-address contract ──────────────────────────────────\n",
        "    SMA2_ZSET(Z_BR_STOP);\n    // ── (1) Stop-address contract ──────────────────────────────────\n")
    replace_once(aborts,
        "    // Snapshot the entry stack so we can restore it exactly (minus the popped\n",
        "    SMA2_ZSET(Z_BR_VEC);\n    // Snapshot the entry stack so we can restore it exactly (minus the popped\n")
    replace_once(aborts,
        "    // ── (2) Enter the interpreter at target_pc ─────────────────────\n",
        "    SMA2_ZSET(Z_BR_ENTRY);\n    // ── (2) Enter the interpreter at target_pc ─────────────────────\n")
    replace_once(aborts,
        "        if (gba_mod_function_entry(pc, was_thumb ? 1 : 0, &g_cpu)) {\n",
        "        SMA2_ZSET(Z_MODENTRY);\n        if (gba_mod_function_entry(pc, was_thumb ? 1 : 0, &g_cpu)) {\n")
    replace_once(aborts,
        "#if defined(SMA2_CART_AOT_IRQ_DIAG)\n        sma2_aot_irq_instruction_boundary",
        "        SMA2_ZSET(Z_BRLOOP);\n#if defined(SMA2_CART_AOT_IRQ_DIAG)\n        sma2_aot_irq_instruction_boundary")
    replace_once(aborts,
        "        // Call-nesting pre-flight update (see the call_nest declaration above).\n",
        "        SMA2_ZSET(Z_CALLNEST);\n        // Call-nesting pre-flight update (see the call_nest declaration above).\n")
    replace_once(aborts,
        "    if (gba_mod_function_entry(g_cpu.R[15],\n",
        "    SMA2_ZSET(Z_MODENTRY);\n    if (gba_mod_function_entry(g_cpu.R[15],\n")
    replace_once(aborts,
        "    g_runtime_force_interp_step_active = 1;\n    const bool should_yield",
        "    SMA2_ZSET(Z_YIELD);\n    g_runtime_force_interp_step_active = 1;\n    const bool should_yield")
    # forced-interpreter step (configuration C)
    replace_once(aborts,
        'extern "C" void runtime_force_interp_step(void) {\n    using gbarecomp::active_bus;\n',
        'extern "C" void runtime_force_interp_step(void) {\n    SMA2_PZ_OUTER(Z_YIELD);\n    using gbarecomp::active_bus;\n')
    replace_once(aborts,
        "    if (should_yield) return;\n\n    armv4t::CPUState cpu{};\n",
        "    if (should_yield) return;\n    SMA2_ZSET(Z_SYNC);\n\n    armv4t::CPUState cpu{};\n")
    replace_once(aborts,
        "    armv4t::Instr insn{};\n    if (cpu.thumb) {\n",
        "    SMA2_ZSET(Z_FETCH);\n    armv4t::Instr insn{};\n    if (cpu.thumb) {\n")
    replace_once(aborts,
        "    ForceInterpBus fib(bus);\n",
        "    SMA2_ZSET(Z_STEP);\n    ForceInterpBus fib(bus);\n")
    replace_once(aborts,
        "    if (r == armv4t::Interpreter::Result::NotImplemented ||\n"
        "        r == armv4t::Interpreter::Result::Undefined) {\n"
        "        // A genuine interpreter gap. The interpreter is the reference model here;",
        "    SMA2_ZSET(Z_SYNC);\n"
        "    if (r == armv4t::Interpreter::Result::NotImplemented ||\n"
        "        r == armv4t::Interpreter::Result::Undefined) {\n"
        "        // A genuine interpreter gap. The interpreter is the reference model here;")
    replace_once(aborts,
        '        g_tick_ctx = "finterp"; runtime_tick(cyc); g_tick_ctx = "gen";\n',
        '        SMA2_ZSET(Z_TICK);\n'
        '        g_tick_ctx = "finterp"; runtime_tick(cyc); g_tick_ctx = "gen";\n'
        '        SMA2_ZSET(Z_SYNC);\n')
    replace_once(aborts,
        "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n    sma2_snapshot_h5_instruction_retired(pc, cpu.thumb ? 1u : 0u);\n#endif\n}\n",
        "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n    SMA2_ZSET(Z_RETIRE);\n    sma2_snapshot_h5_instruction_retired(pc, cpu.thumb ? 1u : 0u);\n#endif\n}\n")
    # dispatch miss + bridge (configurations P/A)
    replace_once(aborts,
        'extern "C" void runtime_dispatch_miss(uint32_t target_pc) {\n',
        'extern "C" void runtime_dispatch_miss(uint32_t target_pc) {\n    SMA2_PZ(Z_MISS);\n')
    replace_once(aborts,
        "                                        std::uint64_t max_instrs) {\n    using gbarecomp::active_bus;\n",
        "                                        std::uint64_t max_instrs) {\n    SMA2_PZ(Z_BR_ENTRY);\n    using gbarecomp::active_bus;\n")
    replace_once(aborts,
        "        gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);\n"
        "        const std::uint32_t pc = cpu.R[15];\n",
        "        SMA2_ZSET(Z_SYNC);\n"
        "        gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);\n"
        "        const std::uint32_t pc = cpu.R[15];\n")
    replace_once(aborts,
        "        g_cpu.R[15] = pc;  // store_interp already set this; explicit for clarity\n",
        "        g_cpu.R[15] = pc;  // store_interp already set this; explicit for clarity\n"
        "        SMA2_ZSET(Z_BRLOOP);\n")
    replace_once(aborts,
        "        // Fetch + decode at the current PC.\n",
        "        SMA2_ZSET(Z_FETCH);\n        // Fetch + decode at the current PC.\n")
    cyc_anchor = ("        std::uint32_t cyc = 1;\n#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n"
                  if parity else
                  "        std::uint32_t cyc = 1;\n        armv4t::Interpreter::Result r =\n")
    replace_once(aborts,
        cyc_anchor,
        "        SMA2_ZSET(Z_STEP);\n" + cyc_anchor)
    replace_once(aborts,
        "        ++bridged_insns;\n",
        "        ++bridged_insns;\n        SMA2_ZSET(Z_BRLOOP);\n")
    replace_once(aborts,
        "            gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);\n"
        "            if (r == armv4t::Interpreter::Result::ExceptionReturn) {\n"
        "                runtime_note_interpreted_exception_return(old_mode);\n"
        "            }\n"
        '            g_tick_ctx = "bridge"; runtime_tick(cyc); g_tick_ctx = "gen";\n'
        "            gbarecomp::load_arm_cpu_into_interp(g_cpu, cpu);\n",
        "            SMA2_ZSET(Z_SYNC);\n"
        "            gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);\n"
        "            if (r == armv4t::Interpreter::Result::ExceptionReturn) {\n"
        "                runtime_note_interpreted_exception_return(old_mode);\n"
        "            }\n"
        "            SMA2_ZSET(Z_TICK);\n"
        '            g_tick_ctx = "bridge"; runtime_tick(cyc); g_tick_ctx = "gen";\n'
        "            SMA2_ZSET(Z_SYNC);\n"
        "            gbarecomp::load_arm_cpu_into_interp(g_cpu, cpu);\n"
        "            SMA2_ZSET(Z_BRLOOP);\n")
    replace_once(aborts,
        "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n        sma2_snapshot_h5_instruction_retired(pc, was_thumb ? 1u : 0u);\n#endif\n",
        "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n        SMA2_ZSET(Z_RETIRE);\n        sma2_snapshot_h5_instruction_retired(pc, was_thumb ? 1u : 0u);\n        SMA2_ZSET(Z_BRLOOP);\n#endif\n")
    replace_once(aborts,
        "    g_interp_insns += bridged_insns;\n\n    // ── (4) Write back",
        "    g_interp_insns += bridged_insns;\n    SMA2_ZSET_NC(Z_BR_ENTRY);\n\n    // ── (4) Write back")


def apply_fast_miss(dest: pathlib.Path) -> None:
    """Opt-in (SMA2_BRIDGE_FAST_MISS): read GBARECOMP_STRICT_STATIC once.

    runtime_dispatch_miss called std::getenv on every miss; in the parity bridge
    that is one libc environment scan per guest instruction. The process
    environment is not mutated after startup (main.cpp only calls setenv during
    startup, before the runtime runs), so memoizing the parsed value is
    behavior-identical. The abort path for a set variable is unchanged. The
    miss-map lookup was evaluated and deliberately NOT changed (no measured
    benefit).
    """
    aborts = dest / "src/runtime/runtime_arm_default_aborts.cpp"
    replace_once(aborts,
        "    const char* strict_env = std::getenv(\"GBARECOMP_STRICT_STATIC\");\n"
        "    const bool strict_static =\n"
        "        strict_env && strict_env[0] != '\\0' && strict_env[0] != '0';\n",
        "    static const bool strict_static = [] {\n"
        "        const char* e = std::getenv(\"GBARECOMP_STRICT_STATIC\");\n"
        "        return e && e[0] != '\\0' && e[0] != '0';\n"
        "    }();\n")


def apply_bridge_batch(dest: pathlib.Path, cap: int, defer: bool,
                       check: bool, stats: bool) -> None:
    """Opt-in bridge state-copy deferral and top-level batching (see
    docs/results/SMA2_AOT_BRIDGE_BATCHING_20261017.md for the proof sketch)."""
    aborts = dest / "src/runtime/runtime_arm_default_aborts.cpp"
    text = aborts.read_text()
    start = text.index('extern "C" int runtime_bridge_interpret(')
    end = text.index("// ── Whole-program force-interpreter backend")
    region = text[start:end]
    region = region.replace("gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);",
                            "sma2_st_cpu(cpu);")
    region = region.replace("gbarecomp::load_arm_cpu_into_interp(g_cpu, cpu);",
                            "sma2_ld_cpu(cpu);")
    text = text[:start] + region + text[end:]
    aborts.write_text(text)

    helpers = (
        "#include <cstring>\n"
        'extern "C" unsigned long long g_sma2_bs[16];\n'
        'extern "C" bool sma2_snapshot_h5_should_stop();\n'
        'extern "C" unsigned long long g_runtime_irq_entries;\n'
        'extern "C" unsigned long long g_runtime_vblank_starts;\n'
        + ("#define SMA2_BS(i) (++g_sma2_bs[(i)])\n" if stats else "#define SMA2_BS(i) ((void)0)\n") +
        "namespace {\n"
        "inline void sma2_st_cpu(const armv4t::CPUState& c) {\n"
        "    SMA2_BS(4);\n    gbarecomp::store_interp_into_arm_cpu(c, g_cpu);\n}\n"
        "inline void sma2_ld_cpu(armv4t::CPUState& c) {\n"
        "    SMA2_BS(5);\n    gbarecomp::load_arm_cpu_into_interp(g_cpu, c);\n}\n"
        "[[maybe_unused]] void sma2_check_synced(const armv4t::CPUState& c, const char* where) {\n"
        "    decltype(g_cpu) t = g_cpu;\n"
        "    gbarecomp::store_interp_into_arm_cpu(c, t);\n"
        "    if (std::memcmp(&t, &g_cpu, sizeof(t)) != 0) {\n"
        '        std::fprintf(stderr, "SMA2_COPY_CHECK_FAIL at %s: interpreter CPU and g_cpu differ\\n", where);\n'
        "        std::abort();\n    }\n}\n"
        "}  // namespace\n\n")
    replace_once(aborts, 'extern "C" int runtime_bridge_interpret(uint32_t entry_pc',
                 helpers + 'extern "C" int runtime_bridge_interpret(uint32_t entry_pc')
    chk = (lambda w: f'        sma2_check_synced(cpu, "{w}");\n') if check else (lambda w: "")
    # cap becomes mutable
    replace_once(aborts,
        "    const std::uint64_t cap = 1u; // one interpreted guest instruction per runner boundary\n",
        "    std::uint64_t cap = 1u; // one interpreted guest instruction per runner boundary\n")
    batch = cap > 1
    replace_once(aborts,
        "    const bool irq_cont = (g_irq_nest_depth != 0u) && (forced_stop_pc == 0u);\n",
        "    const bool irq_cont = (g_irq_nest_depth != 0u) && (forced_stop_pc == 0u);\n"
        "    SMA2_BS(top_level ? 0 : 1);\n"
        + ("""    const bool sma2_batch_ok = top_level && !irq_cont && forced_stop_pc == 0u &&
        max_instrs == 1u && g_runtime_force_interp_hook == nullptr &&
        g_runtime_ram_dispatch_hook == nullptr && !runtime_trace_enabled();
    if (sma2_batch_ok) cap = %d;
    const unsigned long long sma2_vbl0 = g_runtime_vblank_starts;
""" % cap if batch else ""))
    replace_once(aborts,
        "    std::uint64_t bridged_insns = 0;\n    for (;;) {\n",
        "    std::uint64_t bridged_insns = 0;\n"
        + ("    struct Sma2BsGuard { std::uint64_t& n; bool top; ~Sma2BsGuard() { g_sma2_bs[top ? 2 : 3] += n; } }\n"
        "        sma2_bs_guard{bridged_insns, top_level};\n" if stats else "") +
        "    for (;;) {\n"
        "        bool sma2_event = false;\n")
    if defer:
        replace_once(aborts,
            "        sma2_st_cpu(cpu);\n        const std::uint32_t pc = cpu.R[15];\n",
            "        // Deferred: from the second instruction of a batch g_cpu already equals cpu\n"
            "        // (post-tick state), so the pre-execution publish is redundant.\n"
            "        if (iters == 0) sma2_st_cpu(cpu);\n"
            "        else {\n" + chk("batch-top") + "        }\n"
            "        const std::uint32_t pc = cpu.R[15];\n")
        replace_once(aborts,
            "            sma2_st_cpu(cpu);\n"
            "            if (r == armv4t::Interpreter::Result::ExceptionReturn) {\n"
            "                runtime_note_interpreted_exception_return(old_mode);\n"
            "            }\n"
            '            g_tick_ctx = "bridge"; runtime_tick(cyc); g_tick_ctx = "gen";\n'
            "            sma2_ld_cpu(cpu);\n",
            "            sma2_st_cpu(cpu);\n"
            "            if (r == armv4t::Interpreter::Result::ExceptionReturn) {\n"
            "                runtime_note_interpreted_exception_return(old_mode);\n"
            "                sma2_event = true;\n"
            "            }\n"
            "            const unsigned long long sma2_irq0 = g_runtime_irq_entries;\n"
            '            g_tick_ctx = "bridge"; runtime_tick(cyc); g_tick_ctx = "gen";\n'
            "            if (g_runtime_irq_entries != sma2_irq0) { sma2_ld_cpu(cpu); sma2_event = true; }\n"
            "            else {\n" + chk("after-tick") + "            }\n")
        replace_once(aborts,
            "    sma2_st_cpu(cpu);  // g_cpu.R[15] == stop_pc\n",
            chk("exit").replace("        ", "    ", 1) if check else "")
        replace_once(aborts,
            "                sma2_st_cpu(cpu);\n                return 0;\n            }\n            sma2_st_cpu(cpu);\n            std::fprintf(stderr,",
            "" + chk("cap-exit").replace("        ", "                ", 1) + "                return 0;\n            }\n            sma2_st_cpu(cpu);\n            std::fprintf(stderr,")
    else:
        pass
    # SWI path: always an event
    replace_once(aborts,
        "            runtime_swi(insn.swi_imm);\n            sma2_ld_cpu(cpu);\n",
        "            runtime_swi(insn.swi_imm);\n            sma2_ld_cpu(cpu);\n            sma2_event = true;\n")
    if batch:
        replace_once(aborts,
            "#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n        if (++iters >= cap) {\n#else\n",
            "#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n"
            "        ++iters;\n"
            "        if (sma2_batch_ok && iters < cap) {\n"
            "            if (sma2_event) break;  // IRQ/SWI/exception return: back to the runner, as at cap 1\n"
            "            const std::uint32_t npc = cpu.R[15] & ~1u;\n"
            "            const bool go = !sma2_snapshot_h5_should_stop() && !bus->io().halted() &&\n"
            "                g_runtime_vblank_starts == sma2_vbl0 &&\n"
            "                !runtime_has_static_entry(cpu.R[15], cpu.thumb ? 1 : 0);\n"
            "            if (!go) break;\n"
            "            // Emulate what the runner + dispatch miss + bridge entry would do for\n"
            "            // the next instruction at cap 1 (no native entry here, by the test above).\n"
            "            SMA2_BS(6);\n"
            "            record_and_log_miss(npc, cpu.thumb);\n"
            "            entry_pc = npc;\n"
            "            stop_pc = g_cpu.R[14] & ~1u;\n"
            "            { MissRec& mr = g_misses[entry_pc];\n"
            "              if (!mr.toplevel_logged) {\n"
            "                  mr.toplevel_logged = true;\n"
            "                  if (self_heal_verbose()) std::fprintf(stderr,\n"
            '                      "runtime_arm: SELF-HEAL bridge entered at top level "\n'
            '                      "(empty call-return stack) for pc=0x%08X; LR=0x%08X "\n'
            '                      "(fallback stop) - will heal to the next static entry. "\n'
            '                      "[logged once/PC; see exit miss-report for counts]\\n",\n'
            "                      entry_pc, stop_pc);\n"
            "              } }\n"
            "            call_nest.clear();\n"
            "            g_bridge_max_call_depth = 0;\n"
            "            continue;\n"
            "        }\n"
            "        if (iters >= cap) {\n#else\n")
    aborts.write_text(aborts.read_text())


def apply_ppu_compositor_cost_diag(dest: pathlib.Path) -> None:
    """Time software scanlines with the KOS clock in the staged copy."""
    ppu = dest / "src/gba/gba_ppu.cpp"
    replace_once(
        ppu,
        "#include <algorithm>\n#include <cstddef>\n#include <cstdint>\n",
        "#include <algorithm>\n#include <cstddef>\n"
        "#include <cstdint>\n#include <cstdlib>\n#include <kos/timer.h>\n",
    )
    replace_once(
        ppu,
        "namespace gba {\n",
        "namespace gba {\n\n"
        "#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)\n"
        "namespace {\n"
        "std::uint64_t sma2_compositor_us = 0;\n"
        "std::uint64_t sma2_compositor_calls = 0;\n"
        "void report_sma2_compositor_cost() {\n"
        "    std::fprintf(stderr, \"SMA2_PPU_COMPOSITOR_COST calls=%llu total_us=%llu\\n\",\n"
        "        static_cast<unsigned long long>(sma2_compositor_calls),\n"
        "        static_cast<unsigned long long>(sma2_compositor_us));\n"
        "}\n"
        "const bool sma2_compositor_report_registered = [] {\n"
        "    std::atexit(report_sma2_compositor_cost);\n"
        "    return true;\n"
        "}();\n"
        "}\n"
        "extern \"C\" std::uint64_t sma2_ppu_compositor_total_us() {\n"
        "    return sma2_compositor_us;\n"
        "}\n"
        "#endif\n",
    )
    replace_once(
        ppu,
        "        render_scanline_internal(work_fb_.data(), rgb565_work, y, dispcnt,\n"
        "                                 affine_io.data(), vram, oam, pal,\n"
        "                                 kScreenWidth, kScreenHeight);\n",
        "#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)\n"
        "        const std::uint64_t sma2_compositor_start = timer_us_gettime64();\n"
        "#endif\n"
        "        render_scanline_internal(work_fb_.data(), rgb565_work, y, dispcnt,\n"
        "                                 affine_io.data(), vram, oam, pal,\n"
        "                                 kScreenWidth, kScreenHeight);\n"
        "#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)\n"
        "        sma2_compositor_us += timer_us_gettime64() - sma2_compositor_start;\n"
        "        ++sma2_compositor_calls;\n"
        "#endif\n",
    )


def apply_ppu_scanline_cost_diag(dest: pathlib.Path) -> None:
    """Time the complete GbaPpu::render_scanline body in the staged PPU."""
    ppu = dest / "src/gba/gba_ppu.cpp"
    replace_once(
        ppu,
        "#include <cstdint>\n",
        "#include <cstdint>\n"
        "#if defined(SMA2_PPU_SCANLINE_COST_DIAG)\n"
        "#include <cstdlib>\n#include <kos/timer.h>\n"
        "#endif\n",
    )
    replace_once(
        ppu,
        "namespace gba {\n",
        "namespace gba {\n"
        "#if defined(SMA2_PPU_SCANLINE_COST_DIAG)\n"
        "namespace {\n"
        "std::uint64_t sma2_scanline_body_us = 0;\n"
        "std::uint64_t sma2_scanline_body_calls = 0;\n"
        "void report_sma2_scanline_body_cost() {\n"
        "    std::fprintf(stderr, \"SMA2_PPU_SCANLINE_COST calls=%llu total_us=%llu boundary=GbaPpu::render_scanline_body\\n\",\n"
        "        static_cast<unsigned long long>(sma2_scanline_body_calls),\n"
        "        static_cast<unsigned long long>(sma2_scanline_body_us));\n"
        "}\n"
        "__attribute__((noinline)) void finish_sma2_scanline_body(std::uint64_t start) {\n"
        "    sma2_scanline_body_us += timer_us_gettime64() - start;\n"
        "    ++sma2_scanline_body_calls;\n"
        "}\n"
        "const bool sma2_scanline_body_report_registered = [] {\n"
        "    std::atexit(report_sma2_scanline_body_cost);\n"
        "    return true;\n"
        "}();\n"
        "struct ScanlineBodyTimer {\n"
        "    std::uint64_t start = timer_us_gettime64();\n"
        "    ~ScanlineBodyTimer() {\n"
        "        finish_sma2_scanline_body(start);\n"
        "    }\n"
        "};\n"
        "}\n"
        "#endif\n",
    )
    replace_once(
        ppu,
        "                             const uint8_t* pal) {\n"
        "    if (presentation_) presentation_->scanline",
        "                             const uint8_t* pal) {\n"
        "#if defined(SMA2_PPU_SCANLINE_COST_DIAG)\n"
        "    const ScanlineBodyTimer sma2_scanline_body_timer{};\n"
        "#endif\n"
        "    if (presentation_) presentation_->scanline",
    )


def apply_no_tls(dest: pathlib.Path) -> None:
    """Remove thread-local storage from the Dreamcast overlay.

    A thread-local object makes the linker emit a PT_TLS program header whose
    .tbss shares the address of .ctors. nullDC3's REIOS loader
    (reios_elf.cpp) loads every program header regardless of p_type and
    zero-fills memsz - filesz, so PT_TLS erases the first 0x40 bytes of
    .ctors and ___do_global_ctors_aux jumps to 0. The Dreamcast runner has one
    guest thread, so the TLS qualifier carries no meaning here.
    """
    header = dest / "src/armv4t/runtime_arm.h"
    replace_once(header, "#  define GBARECOMP_TAIL_ARG_TLS thread_local\n",
                 "#  define GBARECOMP_TAIL_ARG_TLS\n")
    replace_once(header, "#  define GBARECOMP_TAIL_ARG_TLS _Thread_local\n",
                 "#  define GBARECOMP_TAIL_ARG_TLS\n")
    replace_once(dest / "src/gba/gba_bus.cpp",
                 "static thread_local uint8_t scratch[64];",
                 "static uint8_t scratch[64];")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: prepare_dreamcast_framework.py GBARECOMP_ROOT DEST", file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1]).resolve()
    dest = pathlib.Path(sys.argv[2]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    print(f"GBARecomp source revision: {revision}")
    rgb565_sidecar = os.environ.get("SMA2_RGB565_SIDECAR", "OFF")
    if rgb565_sidecar not in ("ON", "OFF"):
        raise SystemExit("SMA2_RGB565_SIDECAR must be ON or OFF")
    h5_diag = os.environ.get("SMA2_SNAPSHOT_H5_DIAG", "OFF")
    if h5_diag not in ("ON", "OFF"):
        raise SystemExit("SMA2_SNAPSHOT_H5_DIAG must be ON or OFF")
    parity_diag = os.environ.get("SMA2_AOT_MODE_PARITY_DIAG", "OFF")
    if parity_diag not in ("ON", "OFF"):
        raise SystemExit("SMA2_AOT_MODE_PARITY_DIAG must be ON or OFF")
    if parity_diag == "ON" and h5_diag != "ON":
        raise SystemExit("SMA2_AOT_MODE_PARITY_DIAG requires SMA2_SNAPSHOT_H5_DIAG=ON")
    gate9d_treatment = os.environ.get("SMA2_GATE9D_TREATMENT", "ON")
    if gate9d_treatment not in ("ON", "OFF"):
        raise SystemExit("SMA2_GATE9D_TREATMENT must be ON or OFF")
    compositor_cost_diag = os.environ.get("SMA2_PPU_COMPOSITOR_COST_DIAG", "OFF")
    if compositor_cost_diag not in ("ON", "OFF"):
        raise SystemExit("SMA2_PPU_COMPOSITOR_COST_DIAG must be ON or OFF")
    scanline_cost_diag = os.environ.get("SMA2_PPU_SCANLINE_COST_DIAG", "OFF")
    if scanline_cost_diag not in ("ON", "OFF"):
        raise SystemExit("SMA2_PPU_SCANLINE_COST_DIAG must be ON or OFF")
    if rgb565_sidecar == "ON":
        validated_rgb565_inputs = {
            "src/gba/gba_ppu.h": "638e35559f4de833e790bcc3944a15d3a6198ca211a093e6f02909bdcdfb11dc",
            "src/gba/gba_ppu.cpp": "d35bc07858f23d1ad280aab81b7f6ec44c8e15cec31311b4035575a3ba0786b3",
            "src/gba/gba_simulation_state.cpp": "20fd118674440cca5de513fbf3bb632cc8f5f9326696e05015e3a23a29aa4527",
        }
        for relative_path, expected in validated_rgb565_inputs.items():
            path = source / relative_path
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != expected:
                raise SystemExit(
                    f"RGB565 sidecar input changed from host-validated source: {path}\n"
                    f"  expected SHA256 {expected}\n  actual SHA256   {actual}")
            print(f"RGB565 GBARecomp input SHA256 {path.name}: {actual}")
        header_text = (source / "src/gba/gba_ppu.h").read_text()
        for api in ("set_rgb565_sidecar_enabled(bool enabled)",
                    "latched_framebuffer_rgb565() const"):
            if api not in header_text:
                raise SystemExit(
                    f"RGB565 sidecar requested but selected GBARecomp lacks {api}: {source}")
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(source, dest, ignore=shutil.ignore_patterns(".git", "build", "build-*"))

    if compositor_cost_diag == "ON":
        apply_ppu_compositor_cost_diag(dest)
    if scanline_cost_diag == "ON":
        apply_ppu_scanline_cost_diag(dest)

    if rgb565_sidecar == "ON":
        host_header = dest / "src/runtime/host_window.h"
        replace_once(
            host_header,
            "    void present(const uint8_t* rgb888);\n",
            "    void present(const uint8_t* rgb888);\n"
            "    bool present_rgb565(const uint16_t* rgb565);\n",
        )

    # In SMA2's interpreter fallback, symbol attribution is only consumed by
    # verbose per-PC diagnostics. Keep first-miss bookkeeping and the independent
    # trace-on-miss path intact, but avoid map lookup/formatting when verbose is
    # disabled. This patches only the staged copy; GBARecomp remains untouched.
    gate9d_source = dest / "src/runtime/runtime_arm_default_aborts.cpp"
    gate9d_before = """        char symbuf[96];
        symbuf[0] = '\\0';
        std::uint32_t off = 0;
        const char* sym = gba_symbol_lookup(pc, &off);
        if (sym) std::snprintf(symbuf, sizeof(symbuf), " <near %s+0x%X>", sym, off);
        if (self_heal_verbose()) {
            std::fprintf(stderr,
"""
    gate9d_after = """        if (self_heal_verbose()) {
            char symbuf[96];
            symbuf[0] = '\\0';
            std::uint32_t off = 0;
            const char* sym = gba_symbol_lookup(pc, &off);
            if (sym) std::snprintf(symbuf, sizeof(symbuf), " <near %s+0x%X>", sym, off);
            std::fprintf(stderr,
"""
    gate9d_text = gate9d_source.read_text()
    if gate9d_text.count(gate9d_before) != 1 or gate9d_text.count(gate9d_after) != 0:
        raise SystemExit(f"expected exactly one upstream Gate 9D block in {gate9d_source}")
    if gate9d_treatment == "ON":
        gate9d_source.write_text(gate9d_text.replace(gate9d_before, gate9d_after, 1))
    print(f"SMA2 Gate 9D treatment: {gate9d_treatment}")

    replace_once(
        dest / "src/gba/foreign_obj_focus.h",
        "    uint64_t hud_oam_mask_lo;\n    uint64_t hud_oam_mask_hi;",
        "    alignas(8) uint64_t hud_oam_mask_lo;\n    alignas(8) uint64_t hud_oam_mask_hi;",
    )
    # The non-destructive audio capture ring is host observability, not mixer
    # state. It is several MiB and is not read by the Dreamcast output path or
    # serialized in checkpoints. Normal SMA2 Dreamcast builds disable its
    # storage in CMake; AOT diagnostic builds keep it for instrumentation.
    gba_audio = dest / "src/gba/gba_audio.cpp"
    replace_once(
        gba_audio,
        "    cap_ring_.assign(kCapRingSize, CapSample{});",
        "#if !defined(SMA2_DC_DISABLE_AUDIO_CAPTURE_RING)\n"
        "    cap_ring_.assign(kCapRingSize, CapSample{});\n"
        "#endif",
    )
    replace_once(
        gba_audio,
        "void GbaAudio::cap_push(const CapSample& s) {\n"
        "    // Indexed by the absolute sample number that ring_push is about to assign\n"
        "    // (samples_generated_ is bumped by ring_push immediately after). Keeping\n"
        "    // the two in lockstep means cap_ring_[idx % N] holds the same sample the\n"
        "    // playback FIFO received at absolute index idx.\n"
        "    cap_ring_[samples_generated_ % kCapRingSize] = s;\n"
        "}",
        "void GbaAudio::cap_push(const CapSample& s) {\n"
        "#if !defined(SMA2_DC_DISABLE_AUDIO_CAPTURE_RING)\n"
        "    // Indexed alongside ring_push for the optional capture diagnostic.\n"
        "    cap_ring_[samples_generated_ % kCapRingSize] = s;\n"
        "#else\n"
        "    (void)s;\n"
        "#endif\n"
        "}",
    )
    replace_once(
        gba_audio,
        "        cap_push(caps[i]);\n        ring_push(current_samples_[i], current_sample_rates_[i]);",
        "#if !defined(SMA2_DC_DISABLE_AUDIO_CAPTURE_RING)\n"
        "        cap_push(caps[i]);\n"
        "#endif\n"
        "        ring_push(current_samples_[i], current_sample_rates_[i]);",
    )
    # SMA2 SH-4 dynarec backend: the vendored mGBA ARM core needs the scheduler's
    # remaining event budget to size its block runs. g_event_budget is file-static,
    # so expose it read-only. SMA2-local adaptation only; the pinned GBARecomp
    # checkout is untouched.
    replace_once(
        dest / "src/runtime/runtime_bus_bridge.cpp",
        "static long long          g_event_budget   = 0;",
        "static long long          g_event_budget   = 0;\n\n"
        "// SMA2 SH-4 dynarec backend reads the live budget to size block runs.\n"
        "extern \"C\" long long runtime_event_budget(void) { return g_event_budget; }",
    )
    # Title-owned static block hook, after GBARecomp's static table and native
    # overlay tiers. The external checkout remains untouched.
    replace_once(
        dest / "src/armv4t/runtime_arm.cpp",
        'extern "C" int overlay_try_dispatch(uint32_t pc, int thumb);\n',
        'extern "C" int overlay_try_dispatch(uint32_t pc, int thumb);\n'
        "#if defined(SMA2_SH4_AOT_POC)\n"
        'extern "C" int sma2_aot_dispatch_try(uint32_t pc, int thumb);\n'
        "#endif\n",
    )
    # Diagnostic-only guest-instruction tracing in interpreter paths. A
    # runtime_tick wrapper can see an AOT-triggered IRQ only after runtime_irq
    # has driven the complete handler to iret, which is too late for a handler
    # instruction trace on some targets/toolchains.
    replace_once(
        dest / "src/runtime/runtime_arm_default_aborts.cpp",
        'extern "C" uint32_t g_irq_nest_depth;\n',
        'extern "C" uint32_t g_irq_nest_depth;\n'
        "#if defined(SMA2_CART_AOT_IRQ_DIAG)\n"
        'extern "C" void sma2_aot_irq_instruction_boundary(uint32_t pc, int thumb);\n'
        "#endif\n",
    )
    replace_once(
        dest / "src/runtime/runtime_arm_default_aborts.cpp",
        "        bus->set_bios_access_enabled(pc < 0x00004000u);\n"
        "        if (g_runtime_insn_trace) runtime_insn_fp();\n",
        "#if defined(SMA2_CART_AOT_IRQ_DIAG)\n"
        "        sma2_aot_irq_instruction_boundary(pc, cpu.thumb ? 1 : 0);\n"
        "#endif\n"
        "        bus->set_bios_access_enabled(pc < 0x00004000u);\n"
        "        if (g_runtime_insn_trace) runtime_insn_fp();\n",
    )
    replace_once(
        dest / "src/armv4t/runtime_arm.cpp",
        "        throw std::logic_error(message);\n"
        "    }\n"
        "    runtime_dispatch_miss(target_pc);\n",
        "        throw std::logic_error(message);\n"
        "    }\n"
        "#if defined(SMA2_SH4_AOT_POC)\n"
        "    if (sma2_aot_dispatch_try(pc, thumb ? 1 : 0)) return nullptr;\n"
        "#endif\n"
        "    runtime_dispatch_miss(target_pc);\n",
    )
    replace_once(
        dest / "src/gba/gba_color.h",
        "std::min(31u, (tr * eva + br * evb + 8u) >> 4)",
        "std::min<uint32_t>(31u, (tr * eva + br * evb + 8u) >> 4)",
    )
    replace_once(
        dest / "src/gba/gba_color.h",
        "std::min(63u, (tg * eva + bg * evb + 8u) >> 4)",
        "std::min<uint32_t>(63u, (tg * eva + bg * evb + 8u) >> 4)",
    )
    replace_once(
        dest / "src/gba/gba_color.h",
        "std::min(31u, (tb * eva + bb * evb + 8u) >> 4)",
        "std::min<uint32_t>(31u, (tb * eva + bb * evb + 8u) >> 4)",
    )
    replace_once(
        dest / "src/debug/tcp_debug_server.cpp",
        "htonl(INADDR_LOOPBACK)",
        "htonl(0x7f000001u)",
    )
    view_config = dest / "src/runtime/view_config.h"
    for literal, variable, expected in (("240u", "engine_max_width", 1),
                                        ("160u", "engine_max_height", 2)):
        old = f"std::max({literal}, {variable})"
        new = f"std::max<std::uint32_t>({literal}, {variable})"
        count = view_config.read_text().count(old)
        if count != expected:
            raise SystemExit(
                f"expected {expected} SH-4 max type sites in {view_config}, "
                f"found {count}: {old}"
            )
        view_config.write_text(view_config.read_text().replace(old, new))
    replace_once(
        view_config,
        "std::clamp(\n        game_max_width, 240u,",
        "std::clamp<std::uint32_t>(\n        game_max_width, 240u,",
    )
    replace_once(
        view_config,
        "std::clamp(\n        game_max_height, 160u,",
        "std::clamp<std::uint32_t>(\n        game_max_height, 160u,",
    )
    replace_once(
        view_config,
        "std::clamp(game_max_height, 160u,",
        "std::clamp<std::uint32_t>(game_max_height, 160u,",
    )
    replace_once(
        dest / "src/runtime/bios_hle.cpp",
        "div_t res = div(num, denom);",
        "ldiv_t res = ldiv(static_cast<long>(num), static_cast<long>(denom));",
    )
    ppu_header = dest / "src/gba/gba_ppu.h"
    replace_once(
        ppu_header,
        "    static constexpr uint32_t kMaxExtraX = 520;\n"
        "    static constexpr uint32_t kMaxExtraY = 352;",
        "#if defined(SMA2_DREAMCAST) || defined(__sh__)\n"
        "    // SMA2's Dreamcast target is fixed to the native 240x160 view.\n"
        "    // Avoid reserving the desktop's 1280x864 triple-byte surfaces.\n"
        "    static constexpr uint32_t kMaxExtraX = 0;\n"
        "    static constexpr uint32_t kMaxExtraY = 0;\n"
        "#else\n"
        "    static constexpr uint32_t kMaxExtraX = 520;\n"
        "    static constexpr uint32_t kMaxExtraY = 352;\n"
        "#endif",
    )
    runtime_cpp = dest / "src/runtime/runtime.cpp"
    replace_once(
        runtime_cpp,
        '#include "runtime.h"\n',
        '#include "runtime.h"\n'
        '#if defined(SMA2_CART_AOT_DIAG)\n'
        '#include "sma2_cart_aot_diag_result.h"\n'
        '#endif\n',
    )
    replace_once(
        runtime_cpp,
        '        if (const char* e = std::getenv("GBARECOMP_DIAGNOSTICS"); e && *e)\n'
        "            want = e[0] != '0';\n",
        '        if (const char* e = std::getenv("GBARECOMP_DIAGNOSTICS"); e && *e)\n'
        "            want = e[0] != '0';\n"
        "#if defined(SMA2_CART_AOT_DIAG) || defined(SMA2_SH4_DYNAREC)\n"
        "        // Bounded AOT/JIT diagnostics must not construct the 32 MiB replay writer.\n"
        "        // Keep this OFF even when config.ini or environment requests capture.\n"
        "        want = false;\n"
        "#endif\n",
    )
    replace_once(
        runtime_cpp,
        "    auto step_once = [&]() -> bool {\n"
        "        last_step_cycles = 0;\n"
        "        // Game-thread-only: fold any worker-finished native overlays into the\n",
        "    auto step_once = [&]() -> bool {\n"
        "        last_step_cycles = 0;\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "        // Poll dcload's host filesystem occasionally so a diagnostic can\n"
        "        // return through run_game cleanup instead of killing kos-tool.\n"
        "        static uint32_t sma2_exit_flag_poll_steps = 0;\n"
        "        if ((++sma2_exit_flag_poll_steps & 0x7FFFu) == 0) {\n"
        "            constexpr const char* sma2_exit_flag_path = \"/pc/sma2_exit.txt\";\n"
        "            std::ifstream sma2_exit_flag(sma2_exit_flag_path, std::ios::binary);\n"
        "            if (sma2_exit_flag.good()) {\n"
        "                sma2_exit_flag.close();\n"
        "                const int removed = std::remove(sma2_exit_flag_path);\n"
        "                std::fprintf(stderr, \"[sma2] exit flag received; returning through runtime cleanup%s\\n\",\n"
        "                             removed == 0 ? \"\" : \" (flag removal failed; delete it before relaunch)\");\n"
        "                return false;\n"
        "            }\n"
        "        }\n"
        "#endif\n"
        "        // Game-thread-only: fold any worker-finished native overlays into the\n",
    )
    replace_once(
        runtime_cpp,
        'extern "C" unsigned long long g_runtime_irq_entries;\n',
        'extern "C" unsigned long long g_runtime_irq_entries;\n'
        "#if defined(SMA2_CART_AOT_DIAG)\n"
        'extern "C" int sma2_cart_aot_diag_run();\n'
        "#endif\n"
        "#if defined(SMA2_DREAMCAST)\n"
        'extern "C" unsigned long long sma2_dc_interp_dispatches;\n'
        'extern "C" unsigned long long sma2_dc_guest_instructions;\n'
        'extern "C" uint64_t sma2_dc_time_us();\n'
        'extern "C" void sma2_dc_host_perf_snapshot(uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint32_t*, uint32_t*);\n'
        "#endif\n",
    )
    replace_once(
        runtime_cpp,
        "    int diag_exit = 0;\n",
        "    int diag_exit = 0;\n"
        "#if defined(SMA2_CART_AOT_DIAG)\n"
        "    if (sma2_cart_aot_diag_exit > 0)\n"
        "        diag_exit = sma2_cart_aot_diag_exit;\n"
        "#endif\n",
    )
    replace_once(
        runtime_cpp,
        "    auto step_once = [&]() -> bool {\n"
        "        last_step_cycles = 0;\n",
        "#if defined(SMA2_CART_AOT_DIAG)\n"
        "    int sma2_cart_aot_diag_exit = -1;\n"
        "#endif\n"
        "    auto step_once = [&]() -> bool {\n"
        "        last_step_cycles = 0;\n"
        "#if defined(SMA2_CART_AOT_DIAG)\n"
        "        static bool sma2_cart_aot_diag_done = false;\n"
        "        if (!sma2_cart_aot_diag_done) {\n"
        "            sma2_cart_aot_diag_done = true;\n"
        "            sma2_cart_aot_diag_exit =\n"
        "                sma2_cart_aot_diag_report_exit(sma2_cart_aot_diag_run());\n"
        "            return false;\n"
        "        }\n"
        "#endif\n",
    )
    replace_once(
        runtime_cpp,
        "            runtime_dispatch(g_cpu.R[15]);\n"
        "        }\n"
        "        ++taken;\n",
        "            runtime_dispatch(g_cpu.R[15]);\n"
        "        }\n"
        "#if defined(SMA2_SH4_AOT_POC)\n"
        "        if (sma2_aot_diagnostic_after_dispatch()) {\n"
        "            std::fprintf(stderr, \"[sma2-aot-dispatch] bounded continuation complete; stopping runner\\n\");\n"
        "            return false;\n"
        "        }\n"
        "#endif\n"
        "        ++taken;\n",
    )
    # SMA2 SH-4 dynarec backend: the adapter (src/sh4jit/sma2_sh4_backend.cpp)
    # exposes its entry points as C symbols so the runner can select the CPU
    # backend at runtime. Interpreter stays the default and the fallback.
    replace_once(
        runtime_cpp,
        'extern "C" void sma2_dc_host_perf_snapshot(uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint32_t*, uint32_t*);\n'
        "#endif\n",
        'extern "C" void sma2_dc_host_perf_snapshot(uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint32_t*, uint32_t*);\n'
        "#if defined(SMA2_SH4_AOT_POC)\n"
        'extern "C" uint32_t sma2_aot_diagnostic_step(void);\n'
        'extern "C" uint32_t sma2_aot_diagnostic_after_dispatch(void);\n'
        "#endif\n"
        "#if defined(SMA2_SH4_DYNAREC)\n"
        'extern "C" int sma2_dc_jit_init(void);\n'
        'extern "C" int sma2_dc_jit_drive(uint32_t*, uint32_t*);\n'
        'extern "C" uint32_t sma2_dc_jit_region_allowed(uint32_t);\n'
        'extern "C" uint32_t sma2_dc_jit_diagnostic_complete(void);\n'
        'extern "C" uint32_t sma2_dc_jit_diagnostic_step(void);\n'
        'extern "C" void sma2_dc_jit_print_final_summary(void);\n'
        'extern "C" void sma2_dc_jit_snapshot(uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*);\n'
        'extern "C" void sma2_dc_jit_trace_push_runtime(const char*, uint32_t, unsigned long long);\n'
        "unsigned long long sma2_dc_jit_steps = 0;\n"
        "unsigned long long sma2_dc_jit_blocks = 0;\n"
        "unsigned long long sma2_dc_jit_fallbacks = 0;\n"
        "#endif\n"
        "#endif\n",
    )
    # Runtime CPU backend select. Interpreter default; SMA2_CPU_BACKEND=jit asks
    # for the SH-4 dynarec. The bus does not exist at runner startup, so JIT
    # initialization is deferred until the first step boundary.
    replace_once(
        runtime_cpp,
        "    g_force_interp = 0;\n",
        "#if defined(SMA2_SH4_DYNAREC)\n"
        "    // Runtime CPU backend select. The interpreter remains the default.\n"
        "    int sma2_cpu_backend = 0;\n"
        "    if (const char* be = std::getenv(\"SMA2_CPU_BACKEND\"))\n"
        "        sma2_cpu_backend = (be[0] == 'j' || be[0] == 'J') ? 1 : 0;\n"
        "    std::fprintf(stderr, \"sma2_cpu_backend=%s\\n\",\n"
        "                 sma2_cpu_backend ? \"sh4-dynarec (pending bus init)\" : \"interpreter\");\n"
        "#endif\n"
        "    g_force_interp = 0;\n",
    )
    # Backend routing at the runner's per-step boundary.
    replace_once(
        runtime_cpp,
        "        const uint32_t step_pc = g_cpu.R[15] & ~1u;\n"
        "        const int step_thumb = (g_cpu.cpsr & CPSR_T_BIT) != 0;\n"
        "        if (g_force_interp ||\n",
        "#if defined(SMA2_SH4_DYNAREC)\n"
        "        if (sma2_cpu_backend == 1 && sma2_dc_jit_diagnostic_step()) {\n"
        "            std::fprintf(stderr, \"[sma2-jit-diff] stopping outer runner at step budget\\n\");\n"
        "            sma2_dc_jit_print_final_summary();\n"
        "            return false;\n"
        "        }\n"
        "#endif\n"
        "        const uint32_t step_pc = g_cpu.R[15] & ~1u;\n"
        "        const int step_thumb = (g_cpu.cpsr & CPSR_T_BIT) != 0;\n"
        "#if defined(SMA2_SH4_AOT_POC)\n"
        "        if (sma2_aot_diagnostic_step()) {\n"
        "            std::fprintf(stderr, \"[sma2-aot] stopping outer runner after bounded diagnostic\\n\");\n"
        "            return false;\n"
        "        }\n"
        "#endif\n"
        "#if defined(SMA2_SH4_DYNAREC)\n"
        "        // SH-4 dynarec branch. It runs only for the regions the backend\n"
        "        // translates (see sma2_dc_jit_region_allowed); BIOS and mutable\n"
        "        // guest RAM fall through to the production interpreter below, so\n"
        "        // the fallback is visible, never hidden inside the dynarec.\n"
        "        if (sma2_cpu_backend == 1 && sma2_dc_jit_region_allowed(step_pc)) {\n"
        "            static bool sma2_push_diag_logged = false;\n"
        "            uint32_t jit_steps_blocks = 0, jit_steps_fallbacks = 0;\n"
        "            const int jit_handled =\n"
        "                sma2_dc_jit_drive(&jit_steps_blocks, &jit_steps_fallbacks);\n"
        "            sma2_dc_jit_blocks += jit_steps_blocks;\n"
        "            sma2_dc_jit_fallbacks += jit_steps_fallbacks;\n"
        "            if (sma2_dc_jit_diagnostic_complete()) {\n"
        "                std::fprintf(stderr, \"[sma2-jit-diff] stopping outer runner after boundary\\n\");\n"
        "                sma2_dc_jit_print_final_summary();\n"
        "                return false;\n"
        "            }\n"
        "            const bool trace_sma2_push = !sma2_push_diag_logged &&\n"
        "                (g_cpu.cpsr & CPSR_T_BIT) && g_cpu.R[15] == 0x0800044Cu;\n"
        "            const uint32_t sma2_push_sp = g_cpu.R[13];\n"
        "            if (trace_sma2_push) {\n"
        "                sma2_push_diag_logged = true;\n"
        "                sma2_dc_jit_trace_push_runtime(\"after-jit-handoff\",\n"
        "                    sma2_push_sp, g_runtime_cycles);\n"
        "            }\n"
        "            if (jit_handled) {\n"
        "                if (trace_sma2_push)\n"
        "                    sma2_dc_jit_trace_push_runtime(\"after-jit\",\n"
        "                        sma2_push_sp, g_runtime_cycles);\n"
        "                sma2_dc_jit_steps += 1;\n"
        "                ++taken;\n"
        "                sync_frame_counter();\n"
        "                return true;\n"
        "            } else {\n"
        "                ++sma2_dc_jit_fallbacks;\n"
        "                if (trace_sma2_push) {\n"
        "                    std::fprintf(stderr, \"[sma2-jit-push] handoff blocks=%u fallbacks=%u pc=%08X cycles=%llu\\n\",\n"
        "                        jit_steps_blocks, jit_steps_fallbacks,\n"
        "                        g_cpu.R[15], g_runtime_cycles);\n"
        "                    sma2_dc_jit_trace_push_runtime(\"before-interpreter\",\n"
        "                        sma2_push_sp, g_runtime_cycles);\n"
        "                }\n"
        "                // A JIT handoff must execute exactly the current guest\n"
        "                // instruction in the reference interpreter. Falling\n"
        "                // through to runtime_dispatch would re-enter AOT code.\n"
        "                runtime_force_interp_step();\n"
        "                if (trace_sma2_push)\n"
        "                    sma2_dc_jit_trace_push_runtime(\"after-interpreter\",\n"
        "                        sma2_push_sp, g_runtime_cycles);\n"
        "                ++taken;\n"
        "                sync_frame_counter();\n"
        "                return true;\n"
        "            }\n"
        "        }\n"
        "#endif\n"
        "        if (g_force_interp ||\n",
    )
    replace_once(
        runtime_cpp,
        "    auto drain_host_audio = [&]() {\n",
        "#if defined(SMA2_DREAMCAST)\n"
        "    uint64_t sma2_audio_samples_generated = 0;\n"
        "#endif\n"
        "    auto drain_host_audio = [&]() {\n",
    )
    replace_once(
        runtime_cpp,
        "        const auto n = bus.audio().drain_samples(audio_buf, 2048);\n"
        "        if (n && !fast_forward_active) {\n",
        "        const auto n = bus.audio().drain_samples(audio_buf, 2048);\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "        sma2_audio_samples_generated += n;\n"
        "#endif\n"
        "        if (n && !fast_forward_active) {\n",
    )
    telemetry = r'''#if defined(SMA2_DREAMCAST)
    // The interpreter runner often reaches frame boundaries through the outer
    // one-instruction loop, not runtime_should_yield's native-dispatch hook.
    // Record both paths. KOS uptime is monotonic; clocks are read only around
    // frame work and emitted once per ten frame boundaries.
    struct Sma2PerfWindow {
        uint64_t start_us = sma2_dc_time_us();
        uint64_t boundary_end_us = start_us;
        uint64_t last_report_us = start_us;
        uint64_t boundaries = 0, last_report_boundaries = 0;
        uint64_t last_report_frame = ppu.frame_count();
        uint64_t last_report_cycles = g_runtime_cycles;
        uint64_t last_report_instructions = sma2_dc_guest_instructions;
        uint64_t last_report_dispatches = sma2_dc_interp_dispatches;
        uint64_t last_report_audio_generated = 0;
        uint64_t last_report_presents = 0;
        uint64_t last_report_vblank_wait = 0, last_report_video_convert = 0;
        uint64_t last_report_audio_submit = 0, last_report_audio_queued = 0;
        uint32_t last_report_audio_consumed = 0;
        uint32_t last_report_audio_silence = 0;
        uint64_t guest_us = 0, render_us = 0, present_us = 0;
        uint64_t audio_work_us = 0, input_us = 0, pacer_us = 0;
    } sma2_perf;
    auto sma2_perf_begin_frame = [&]() {
        const uint64_t now = sma2_dc_time_us();
        sma2_perf.guest_us += now - sma2_perf.boundary_end_us;
        return now;
    };
    auto sma2_perf_end_frame = [&](const char* path, uint64_t guest_frame) {
        ++sma2_perf.boundaries;
        const uint64_t now = sma2_dc_time_us();
        sma2_perf.boundary_end_us = now;
        const bool report = sma2_perf.boundaries == 1 ||
            (sma2_perf.boundaries - sma2_perf.last_report_boundaries) >= 10;
        if (!report) return;

        uint64_t presents = 0, vblank_wait = 0, video_convert = 0;
        uint64_t audio_submit = 0, audio_queued = 0;
        uint32_t audio_consumed = 0;
        uint32_t audio_silence = 0;
        sma2_dc_host_perf_snapshot(&presents, &vblank_wait, &video_convert,
                                   &audio_submit, &audio_queued,
                                   &audio_consumed, &audio_silence);
        const uint64_t elapsed = now - sma2_perf.last_report_us;
        const uint64_t frames = guest_frame - sma2_perf.last_report_frame;
        const uint64_t cycles = g_runtime_cycles - sma2_perf.last_report_cycles;
        const uint64_t instructions = sma2_dc_guest_instructions -
                                      sma2_perf.last_report_instructions;
        const uint64_t dispatches = sma2_dc_interp_dispatches -
                                    sma2_perf.last_report_dispatches;
        const uint64_t audio_generated = sma2_audio_samples_generated -
                                         sma2_perf.last_report_audio_generated;
        const uint64_t presentation_calls = presents -
                                            sma2_perf.last_report_presents;
        const uint64_t sync_us = sma2_perf.pacer_us +
            (vblank_wait - sma2_perf.last_report_vblank_wait);
        const uint64_t fps_milli = elapsed ? frames * 1000000000ull / elapsed : 0;
        const uint64_t ips = elapsed ? instructions * 1000000ull / elapsed : 0;
        const uint64_t cps = elapsed ? cycles * 1000000ull / elapsed : 0;
        std::fprintf(stderr,
            "[sma2-perf] path=%s elapsed_us=%llu interval_us=%llu "
            "guest_frame=%llu delta_frames=%llu fps=%llu.%03llu "
            "instructions=%llu ips=%llu cycles=%llu cps=%llu "
            "interp_dispatches=%llu guest_ms=%llu avg_guest_us_per_frame=%llu "
            "render_ms=%llu present_ms=%llu vblank_wait_ms=%llu "
            "video_convert_ms=%llu audio_work_ms=%llu audio_submit_ms=%llu "
            "audio_generated=%llu audio_queued=%llu audio_consumed_delta=%u audio_silence_delta=%u "
            "input_ms=%llu pacer_wait_ms=%llu sync_ms=%llu presents=%llu "
            "pc=0x%08x state=%s keyinput=0x%04x\n",
            path,
            static_cast<unsigned long long>(now - sma2_perf.start_us),
            static_cast<unsigned long long>(elapsed),
            static_cast<unsigned long long>(guest_frame),
            static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(fps_milli / 1000),
            static_cast<unsigned long long>(fps_milli % 1000),
            static_cast<unsigned long long>(instructions),
            static_cast<unsigned long long>(ips),
            static_cast<unsigned long long>(cycles),
            static_cast<unsigned long long>(cps),
            static_cast<unsigned long long>(dispatches),
            static_cast<unsigned long long>(sma2_perf.guest_us / 1000),
            static_cast<unsigned long long>(frames ? sma2_perf.guest_us / frames : 0),
            static_cast<unsigned long long>(sma2_perf.render_us / 1000),
            static_cast<unsigned long long>(sma2_perf.present_us / 1000),
            static_cast<unsigned long long>((vblank_wait - sma2_perf.last_report_vblank_wait) / 1000),
            static_cast<unsigned long long>((video_convert - sma2_perf.last_report_video_convert) / 1000),
            static_cast<unsigned long long>(sma2_perf.audio_work_us / 1000),
            static_cast<unsigned long long>((audio_submit - sma2_perf.last_report_audio_submit) / 1000),
            static_cast<unsigned long long>(audio_generated),
            static_cast<unsigned long long>(audio_queued - sma2_perf.last_report_audio_queued),
            static_cast<unsigned>(audio_consumed - sma2_perf.last_report_audio_consumed),
            static_cast<unsigned>(audio_silence - sma2_perf.last_report_audio_silence),
            static_cast<unsigned long long>(sma2_perf.input_us / 1000),
            static_cast<unsigned long long>(sma2_perf.pacer_us / 1000),
            static_cast<unsigned long long>(sync_us / 1000),
            static_cast<unsigned long long>(presentation_calls),
            static_cast<unsigned>(g_cpu.R[15] & ~1u),
            (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm",
            static_cast<unsigned>(bus.io().read16(0x130)));
        std::fflush(stderr);

        sma2_perf.last_report_us = now;
        sma2_perf.last_report_boundaries = sma2_perf.boundaries;
        sma2_perf.last_report_frame = guest_frame;
        sma2_perf.last_report_cycles = g_runtime_cycles;
        sma2_perf.last_report_instructions = sma2_dc_guest_instructions;
        sma2_perf.last_report_dispatches = sma2_dc_interp_dispatches;
        sma2_perf.last_report_audio_generated = sma2_audio_samples_generated;
        sma2_perf.last_report_presents = presents;
        sma2_perf.last_report_vblank_wait = vblank_wait;
        sma2_perf.last_report_video_convert = video_convert;
        sma2_perf.last_report_audio_submit = audio_submit;
        sma2_perf.last_report_audio_queued = audio_queued;
        sma2_perf.last_report_audio_consumed = audio_consumed;
        sma2_perf.last_report_audio_silence = audio_silence;
        sma2_perf.guest_us = sma2_perf.render_us = sma2_perf.present_us = 0;
        sma2_perf.audio_work_us = sma2_perf.input_us = sma2_perf.pacer_us = 0;
    };
#endif
'''
    replace_once(
        runtime_cpp,
        "    if (args.window && present_in_place) {\n",
        telemetry + "\n    if (args.window && present_in_place) {\n",
    )
    # Dynarec telemetry alongside the existing interpreter telemetry.
    replace_once(
        runtime_cpp,
        '        std::fprintf(stderr,\n'
        '            "[sma2-perf] path=%s elapsed_us=%llu interval_us=%llu "\n',
        "#if defined(SMA2_SH4_DYNAREC)\n"
        "        if (sma2_cpu_backend) {\n"
        "            uint64_t jit_blocks_compiled = 0, jit_guest_insns_compiled = 0;\n"
        "            uint64_t jit_code_bytes = 0, jit_block_runs = 0;\n"
        "            uint64_t jit_fallback = 0, jit_flushes = 0, jit_invalidations = 0;\n"
        "            uint64_t jit_exits = 0;\n"
        "            sma2_dc_jit_snapshot(&jit_blocks_compiled, &jit_guest_insns_compiled,\n"
        "                                   &jit_code_bytes, &jit_block_runs, &jit_fallback,\n"
        "                                   &jit_flushes, &jit_invalidations, &jit_exits);\n"
        "            // JIT_COUNT is not defined for the SH-4 target, so generated\n"
        "            // code does not count guest instructions: ips is reported as\n"
        "            // unavailable for native block runs rather than guessed.\n"
        "            std::fprintf(stderr,\n"
        "                \"[sma2-jit] backend=sh4-dynarec jit_steps=%llu \"\n"
        "                \"blocks_compiled=%llu guest_insns_compiled=%llu \"\n"
        "                \"code_bytes=%llu block_runs=%llu fallback_steps=%llu \"\n"
        "                \"flushes=%llu invalidations=%llu exits=%llu \"\n"
        "                \"driver_blocks=%llu interpreter_handoffs=%llu \"\n"
        "                \"guest_ips=unavailable(native block runs) interp_dispatches=%llu\\n\",\n"
        "                static_cast<unsigned long long>(sma2_dc_jit_steps),\n"
        "                static_cast<unsigned long long>(jit_blocks_compiled),\n"
        "                static_cast<unsigned long long>(jit_guest_insns_compiled),\n"
        "                static_cast<unsigned long long>(jit_code_bytes),\n"
        "                static_cast<unsigned long long>(jit_block_runs),\n"
        "                static_cast<unsigned long long>(jit_fallback),\n"
        "                static_cast<unsigned long long>(jit_flushes),\n"
        "                static_cast<unsigned long long>(jit_invalidations),\n"
        "                static_cast<unsigned long long>(jit_exits),\n"
        "                static_cast<unsigned long long>(sma2_dc_jit_blocks),\n"
        "                static_cast<unsigned long long>(sma2_dc_jit_fallbacks),\n"
        "                static_cast<unsigned long long>(sma2_dc_interp_dispatches));\n"
        "            std::fflush(stderr);\n"
        "        }\n"
        "#endif\n"
        '        std::fprintf(stderr,\n'
        '            "[sma2-perf] path=%s elapsed_us=%llu interval_us=%llu "\n',
    )
    replace_repeated(
        runtime_cpp,
        "                if (phase_active) fp_t0 = FramePhaseRing::now_ns();\n",
        "#if defined(SMA2_DREAMCAST)\n"
        "                uint64_t sma2_perf_phase = sma2_perf_begin_frame();\n"
        "#endif\n"
        "                if (phase_active) fp_t0 = FramePhaseRing::now_ns();\n",
        2,
    )
    replace_repeated(
        runtime_cpp,
        "                if (phase_active) fp_t1 = FramePhaseRing::now_ns();\n",
        "#if defined(SMA2_DREAMCAST)\n"
        "                sma2_perf.render_us += sma2_dc_time_us() - sma2_perf_phase;\n"
        "                sma2_perf_phase = sma2_dc_time_us();\n"
        "#endif\n"
        "                if (phase_active) fp_t1 = FramePhaseRing::now_ns();\n",
        2,
    )
    replace_repeated(
        runtime_cpp,
        "                if (present_frame) present_live_frame();\n",
        "                if (present_frame) present_live_frame();\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "                sma2_perf.present_us += sma2_dc_time_us() - sma2_perf_phase;\n"
        "                sma2_perf_phase = sma2_dc_time_us();\n"
        "#endif\n",
        2,
    )
    replace_repeated(
        runtime_cpp,
        "                if (phase_active) fp_t3 = FramePhaseRing::now_ns();\n",
        "#if defined(SMA2_DREAMCAST)\n"
        "                sma2_perf.audio_work_us += sma2_dc_time_us() - sma2_perf_phase;\n"
        "                sma2_perf_phase = sma2_dc_time_us();\n"
        "#endif\n"
        "                if (phase_active) fp_t3 = FramePhaseRing::now_ns();\n",
        2,
    )
    replace_repeated(
        runtime_cpp,
        "                if (phase_active) fp_t4 = FramePhaseRing::now_ns();\n",
        "#if defined(SMA2_DREAMCAST)\n"
        "                sma2_perf.input_us += sma2_dc_time_us() - sma2_perf_phase;\n"
        "                sma2_perf_phase = sma2_dc_time_us();\n"
        "#endif\n"
        "                if (phase_active) fp_t4 = FramePhaseRing::now_ns();\n",
        2,
    )
    replace_repeated(
        runtime_cpp,
        "                    if (pacer) pacer->wait_for_next_frame();\n",
        "                    if (pacer) pacer->wait_for_next_frame();\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "                    sma2_perf.pacer_us += sma2_dc_time_us() - sma2_perf_phase;\n"
        "#endif\n",
        2,
    )
    replace_repeated(
        runtime_cpp,
        "                    frame_phase.record(frame, fp_t0, fp_t1, fp_t2, fp_t3,\n"
        "                                       fp_t4, FramePhaseRing::now_ns());\n"
        "                }\n",
        "                    frame_phase.record(frame, fp_t0, fp_t1, fp_t2, fp_t3,\n"
        "                                       fp_t4, FramePhaseRing::now_ns());\n"
        "                }\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "                sma2_perf_end_frame(present_in_place ? \"present-in-place\" : \"outer-loop\", frame);\n"
        "#endif\n",
        2,
    )
    # Count actual production-interpreter calls. `taken` in runtime.cpp counts
    # outer runner steps, but IRQ instructions can execute recursively inside
    # runtime_tick and must be included in guest instruction totals.
    interp_cpp = dest / "src/runtime/runtime_arm_default_aborts.cpp"
    replace_once(
        interp_cpp,
        'extern "C" int g_force_interp = 0;\n',
        'extern "C" int g_force_interp = 0;\n'
        "#if defined(SMA2_DREAMCAST)\n"
        'extern "C" unsigned long long sma2_dc_interp_dispatches = 0;\n'
        'extern "C" unsigned long long sma2_dc_guest_instructions = 0;\n'
        "#endif\n",
    )
    replace_once(
        interp_cpp,
        'extern "C" void runtime_force_interp_step(void) {\n'
        "    using gbarecomp::active_bus;\n"
        "    gba::GbaBus* bus = active_bus();\n"
        "    if (!bus) return;\n",
        'extern "C" void runtime_force_interp_step(void) {\n'
        "    using gbarecomp::active_bus;\n"
        "    gba::GbaBus* bus = active_bus();\n"
        "    if (!bus) return;\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "    ++sma2_dc_interp_dispatches;\n"
        "#endif\n",
    )
    replace_once(
        interp_cpp,
        "    const armv4t::Interpreter::Result r =\n"
        "        armv4t::Interpreter::step(cpu, fib, insn, &cyc);\n",
        "    const armv4t::Interpreter::Result r =\n"
        "        armv4t::Interpreter::step(cpu, fib, insn, &cyc);\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "    if (r != armv4t::Interpreter::Result::NotImplemented &&\n"
        "        r != armv4t::Interpreter::Result::Undefined)\n"
        "        ++sma2_dc_guest_instructions;\n"
        "#endif\n",
    )
    replace_once(
        runtime_cpp,
        "    gba::GbaBus bus;\n    gba::GbaPpu ppu;",
        "#if defined(SMA2_DREAMCAST)\n"
        "    static gba::GbaBus bus;\n"
        "    static gba::GbaPpu ppu;\n"
        "#else\n"
        "    gba::GbaBus bus;\n"
        "    gba::GbaPpu ppu;\n"
        "#endif",
    )
    if rgb565_sidecar == "ON":
        replace_once(
            runtime_cpp,
            "#endif\n    bus.set_bios(&bios);",
            "#endif\n"
            "#if defined(SMA2_RGB565_SIDECAR)\n"
            "    if (!ppu.set_rgb565_sidecar_enabled(true))\n"
            '        std::fprintf(stderr, "[sma2-rgb565] sidecar allocation failed; retaining RGB888 presentation\\n");\n'
            "#endif\n"
            "    bus.set_bios(&bios);",
        )
    for anchor, marker in [
        ("    Args args;", '    std::fprintf(stderr, "[sma2] runtime entered\\n");\n    Args args;'),
        ("    // Resolve BIOS via the picker chain", '    std::fprintf(stderr, "[sma2] config parsed\\n");\n    // Resolve BIOS via the picker chain'),
        ("    // Resolve ROM the same way.", '    std::fprintf(stderr, "[sma2] BIOS path resolved\\n");\n    // Resolve ROM the same way.'),
        ("    gba::GbaBios bios;", '    std::fprintf(stderr, "[sma2] ROM path resolved\\n");\n    gba::GbaBios bios;'),
        ("    std::vector<uint8_t> rom;", '    std::fprintf(stderr, "[sma2] BIOS loaded\\n");\n    std::vector<uint8_t> rom;'),
        ("    if (rom.size() < 0xC0", '    std::fprintf(stderr, "[sma2] ROM loaded: %zu bytes\\n", rom.size());\n    if (rom.size() < 0xC0'),
        ("#if defined(SMA2_DREAMCAST)\n    static gba::GbaBus bus;", '    std::fprintf(stderr, "[sma2] constructing GBA bus and PPU\\n");\n#if defined(SMA2_DREAMCAST)\n    static gba::GbaBus bus;'),
    ]:
        replace_once(runtime_cpp, anchor, marker)
    replace_once(
        runtime_cpp,
        "        auto r = resolve_asset(args.bios, spec, argv[0]);",
        '        std::fprintf(stderr, "[sma2] resolving BIOS path: %s\\n", args.bios.c_str());\n'
        "        auto r = resolve_asset(args.bios, spec, argv[0]);",
    )
    host_platform = dest / "src/runtime/host_platform.cpp"
    replace_once(
        host_platform,
        "    std::FILE* f = std::fopen(path.c_str(), \"rb\");",
        '    std::fprintf(stderr, "[asset] opening %s\\n", path.c_str());\n'
        "    std::FILE* f = std::fopen(path.c_str(), \"rb\");",
    )
    replace_once(
        host_platform,
        "    long sz = std::ftell(f);",
        '    long sz = std::ftell(f);\n'
        '    std::fprintf(stderr, "[asset] ftell %ld\\n", sz);',
    )
    host_window = dest / "src/runtime/host_window.cpp"
    replace_once(
        host_window,
        "#else  // !GBARECOMP_HAVE_SDL2 — stub backend\n\nnamespace gbarecomp {\n",
        "#else  // !GBARECOMP_HAVE_SDL2 — stub backend\n\n"
        "#if defined(SMA2_DREAMCAST)\n"
        '#include "dreamcast_host_window.inc"\n'
        "#else\n\nnamespace gbarecomp {\n",
    )
    replace_once(
        host_window,
        "}  // namespace gbarecomp\n\n#endif  // GBARECOMP_HAVE_SDL2",
        "}  // namespace gbarecomp\n\n#endif // SMA2_DREAMCAST\n\n"
        "#endif  // GBARECOMP_HAVE_SDL2",
    )
    if h5_diag == "ON":
        runtime_arm_default = dest / "src/runtime/runtime_arm_default_aborts.cpp"
        if parity_diag == "ON":
            replace_once(
                runtime_arm_default,
                'extern "C" int runtime_bridge_interpret(uint32_t entry_pc, bool entry_thumb,\n',
                'extern "C" uint8_t bus_read_u8(uint32_t);\n'
                'extern "C" uint16_t bus_read_u16(uint32_t);\n'
                'extern "C" uint32_t bus_read_u32(uint32_t);\n'
                'extern "C" void bus_write_u8(uint32_t, uint8_t);\n'
                'extern "C" void bus_write_u16(uint32_t, uint16_t);\n'
                'extern "C" void bus_write_u32(uint32_t, uint32_t);\n'
                'namespace {\n'
                'struct Sma2ParityBridgeBus : armv4t::Bus {\n'
                '    gba::GbaBus* raw;\n'
                '    explicit Sma2ParityBridgeBus(gba::GbaBus* b) : raw(b) {}\n'
                '    uint8_t read8(uint32_t a) override { return bus_read_u8(a); }\n'
                '    uint16_t read16(uint32_t a) override { return bus_read_u16(a); }\n'
                '    uint32_t read32(uint32_t a) override { return bus_read_u32(a); }\n'
                '    void write8(uint32_t a, uint8_t v) override { bus_write_u8(a, v); }\n'
                '    void write16(uint32_t a, uint16_t v) override { bus_write_u16(a, v); }\n'
                '    void write32(uint32_t a, uint32_t v) override { bus_write_u32(a, v); }\n'
                '    uint32_t access_cycles(uint32_t a, uint8_t w, bool s) const override { return raw->access_cycles(a, w, s); }\n'
                '    uint32_t code_wait(uint32_t pc, bool t, bool s) const override { return raw->code_wait(pc, t, s); }\n'
                '    int32_t prefetch_stall(int32_t w, uint32_t pc, bool t) override { return raw->prefetch_stall(w, pc, t); }\n'
                '};\n'
                '}\n\n'
                'extern "C" int runtime_bridge_interpret(uint32_t entry_pc, bool entry_thumb,\n',
            )
            replace_once(
                runtime_arm_default,
                '        armv4t::Interpreter::Result r =\n'
                '            armv4t::Interpreter::step(cpu, *bus, insn, &cyc);\n',
                '#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n'
                '        Sma2ParityBridgeBus parity_bus(bus);\n'
                '        armv4t::Interpreter::Result r =\n'
                '            armv4t::Interpreter::step(cpu, parity_bus, insn, &cyc);\n'
                '#else\n'
                '        armv4t::Interpreter::Result r =\n'
                '            armv4t::Interpreter::step(cpu, *bus, insn, &cyc);\n'
                '#endif\n',
            )
        replace_once(
            runtime_arm_default,
            'extern "C" int runtime_bridge_interpret(uint32_t entry_pc, bool entry_thumb,\n',
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            'extern "C" void sma2_snapshot_h5_instruction_retired(\n'
            "    uint32_t pc, uint32_t thumb);\n"
            "#endif\n\n"
            'extern "C" int runtime_bridge_interpret(uint32_t entry_pc, bool entry_thumb,\n',
        )
        replace_once(
            runtime_arm_default,
            "    const std::uint64_t cap = max_instrs != 0u ? max_instrs : kBridgeIterationCap;\n",
            "#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n"
            "    const std::uint64_t cap = 1u; // one interpreted guest instruction per runner boundary\n"
            "#else\n"
            "    const std::uint64_t cap = max_instrs != 0u ? max_instrs : kBridgeIterationCap;\n"
            "#endif\n",
        )
        replace_once(
            runtime_arm_default,
            "        if (!irq_cont && (cpu.R[15] & ~1u) == stop_pc) break;\n",
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            "        sma2_snapshot_h5_instruction_retired(pc, was_thumb ? 1u : 0u);\n"
            "#endif\n"
            "        if (!irq_cont && (cpu.R[15] & ~1u) == stop_pc) break;\n",
        )
        replace_once(
            runtime_arm_default,
            "        if (++iters > cap) {\n",
            "#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n"
            "        if (++iters >= cap) {\n"
            "#else\n"
            "        if (++iters > cap) {\n"
            "#endif\n",
        )
        replace_once(
            runtime_arm_default,
            "    runtime_bridge_interpret(entry_pc, entry_thumb, /*forced_stop_pc=*/0u,\n"
            "                             /*max_instrs=*/0u);\n",
            "    runtime_bridge_interpret(entry_pc, entry_thumb, /*forced_stop_pc=*/0u,\n"
            "#if defined(SMA2_AOT_MODE_PARITY_DIAG)\n"
            "                             /*max_instrs=*/1u);\n"
            "#else\n"
            "                             /*max_instrs=*/0u);\n"
            "#endif\n",
        )
        runtime_arm_default = dest / "src/runtime/runtime_arm_default_aborts.cpp"
        replace_once(
            runtime_arm_default,
            "extern \"C\" void runtime_force_interp_step(void) {",
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            'extern "C" void sma2_snapshot_h5_instruction_retired(\n'
            "    uint32_t pc, uint32_t thumb);\n"
            "#endif\n\n"
            'extern "C" void runtime_force_interp_step(void) {',
        )
        replace_once(
            runtime_arm_default,
            "    if (bus->io().halted() && g_cpu.R[15] < 0x00004000u) {\n"
            "        bus->latch_bios_prefetch(g_cpu.R[15], (g_cpu.cpsr & CPSR_T_BIT) != 0);\n"
            "    }\n}\n\n"
            "// runtime_dispatch_miss",
            "    if (bus->io().halted() && g_cpu.R[15] < 0x00004000u) {\n"
            "        bus->latch_bios_prefetch(g_cpu.R[15], (g_cpu.cpsr & CPSR_T_BIT) != 0);\n"
            "    }\n"
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            "    sma2_snapshot_h5_instruction_retired(pc, cpu.thumb ? 1u : 0u);\n"
            "#endif\n"
            "}\n\n"
            "// runtime_dispatch_miss",
        )
        runtime_cpp = dest / "src/runtime/runtime.cpp"
        replace_once(
            runtime_cpp,
            'extern "C" unsigned long long g_runtime_irq_entries;\n',
            'extern "C" unsigned long long g_runtime_irq_entries;\n'
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            'extern "C" bool sma2_snapshot_h5_should_stop();\n'
            "#endif\n",
        )
        replace_once(
            runtime_cpp,
            "            runtime_force_interp_step();\n"
            "        } else {\n"
            "            runtime_dispatch(g_cpu.R[15]);",
            "            runtime_force_interp_step();\n"
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            "            if (sma2_snapshot_h5_should_stop()) return false;\n"
            "#endif\n"
            "        } else {\n"
            "            runtime_dispatch(g_cpu.R[15]);\n"
            "#if defined(SMA2_SNAPSHOT_H5_DIAG)\n"
            "        if (sma2_snapshot_h5_should_stop()) return false;\n"
            "#endif\n",
        )
    if os.environ.get("SMA2_BRIDGE_PROF", "OFF") == "ON":
        if h5_diag != "ON":
            raise SystemExit("SMA2_BRIDGE_PROF requires SMA2_SNAPSHOT_H5_DIAG=ON")
        apply_bridge_prof(dest, pathlib.Path(__file__).resolve().parent.parent,
                          parity_diag == "ON")
    if os.environ.get("SMA2_BRIDGE_FAST_MISS", "OFF") == "ON":
        apply_fast_miss(dest)
    batch_cap = int(os.environ.get("SMA2_BRIDGE_BATCH", "1"))
    defer = os.environ.get("SMA2_BRIDGE_DEFER_COPY", "OFF") == "ON"
    copy_check = os.environ.get("SMA2_BRIDGE_COPY_CHECK", "OFF") == "ON"
    bstats = os.environ.get("SMA2_BRIDGE_STATS", "OFF") == "ON"
    if batch_cap > 1 or defer or bstats:
        if parity_diag != "ON" or os.environ.get("SMA2_BRIDGE_PROF", "OFF") == "ON":
            raise SystemExit("bridge batching/deferral requires parity ON and PROF OFF")
        apply_bridge_batch(dest, batch_cap, defer, copy_check, bstats)
    if rgb565_sidecar == "ON":
        runtime_cpp = dest / "src/runtime/runtime.cpp"
        replace_repeated(
            runtime_cpp,
            "                if (present_frame) {\n"
            "                    const bool view_changed = sync_resize_driven_view();\n",
            "                bool sma2_rgb565_latched_frame = false;\n"
            "                if (present_frame) {\n"
            "                    const bool view_changed = sync_resize_driven_view();\n",
            2,
        )
        replace_repeated(
            runtime_cpp,
            "                    if (ppu.has_latched_framebuffer() && !view_changed) {\n"
            "                        std::memcpy(live_fb.data(), ppu.latched_framebuffer(),\n"
            "                                    ppu.render_bytes());\n"
            "                    } else {\n",
            "                    if (ppu.has_latched_framebuffer() && !view_changed) {\n"
            "                        sma2_rgb565_latched_frame =\n"
            "                            ppu.render_width() == 240 &&\n"
            "                            ppu.render_height() == 160 &&\n"
            "                            ppu.latched_framebuffer_rgb565() != nullptr;\n"
            "                        if (!sma2_rgb565_latched_frame)\n"
            "                            std::memcpy(live_fb.data(), ppu.latched_framebuffer(),\n"
            "                                        ppu.render_bytes());\n"
            "                    } else {\n",
            2,
        )
        replace_repeated(
            runtime_cpp,
            "                if (present_frame) win.present(live_fb.data());\n",
            "                if (present_frame) {\n"
            "                    if (!sma2_rgb565_latched_frame ||\n"
            "                        !win.present_rgb565(ppu.latched_framebuffer_rgb565())) {\n"
            "                        if (sma2_rgb565_latched_frame)\n"
            "                            std::memcpy(live_fb.data(), ppu.latched_framebuffer(),\n"
            "                                        ppu.render_bytes());\n"
            "                        win.present(live_fb.data());\n"
            "                    }\n"
            "                }\n",
            2,
        )
    overlay_compile = dest / "src/runtime/overlay_compile.cpp"
    original_compile = overlay_compile.read_text()
    stub = '''#include "overlay_compile.h"
#if defined(SMA2_DREAMCAST)
namespace gbarecomp {
const char* heal_backend_name(HealBackend b) {
    return b == HealBackend::Tcc ? "tcc" : "gcc";
}
bool overlay_bundled_tcc_available() { return false; }
bool overlay_compile_one(const OverlayWorkItem&, const std::string&,
                         const GbaOverlayCallbacks*, bool, HealBackend,
                         OverlayCompiled*, std::string* err) {
    if (err) *err = "runtime code generation is disabled on Dreamcast";
    return false;
}
}
#else
'''
    overlay_compile.write_text(
        stub + original_compile + "\n#endif // SMA2_DREAMCAST\n"
    )
    apply_no_tls(dest)
    print(f"Staged SMA2 compatibility overlay: {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
