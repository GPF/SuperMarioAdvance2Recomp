#include <algorithm>
#include <chrono>
#include <cstdint>
#include <inttypes.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "gba_audio.h"
#include "gba_bus.h"
#include "gba_io.h"
#include "gba_ppu.h"
#include "runtime_arm.h"
#include "runtime_context.h"
#include "runtime_bus_bridge.h"
#include "self_heal.h"
#include "overlay_loader.h"
#include "snapshot.h"

struct Sma2AotEventObservation {
    uint32_t pc;
    uint16_t if_before;
    uint16_t if_after;
    uint64_t cycle_after;
};
static uint32_t sma2_event_trace_active = 0;
static uint32_t sma2_event_trace_count = 0;
static uint64_t sma2_event_trace_origin = 0;
static Sma2AotEventObservation sma2_event_trace[8] = {};
extern "C" unsigned long long g_runtime_cycles;
extern "C" unsigned long long g_runtime_irq_entries;
extern "C" uint32_t g_irq_nest_depth;
extern "C" void runtime_mmio_catch_up(void);

#if defined(SMA2_CART_AOT_IRQ_DIAG)
struct Sma2IrqTraceRow { uint32_t pc; uint32_t cpsr; uint32_t spsr_irq; };
static uint32_t sma2_irq_trace_active = 0;
static uint32_t sma2_irq_trace_count = 0;
static Sma2IrqTraceRow sma2_irq_trace[512]{};
static uint64_t sma2_irq_stop_baseline = 0;
static uint32_t sma2_irq_stop_active = 0;
static uint32_t sma2_irq_seen = 0;
static uint32_t sma2_irq_post_step_armed = 0;
static uint32_t sma2_irq_stop_pc = 0;
static uint32_t sma2_irq_entry_pc = 0;
static uint32_t sma2_irq_vector_dispatches = 0;
static uint32_t sma2_irq_vector_pc = 0;
static uint32_t sma2_irq_return_arg = 0;
static uint32_t sma2_irq_cpsr_at_entry = 0;
static uint16_t sma2_irq_if_at_entry = 0;
static uint16_t sma2_irq_ie_at_entry = 0;
static uint16_t sma2_irq_ime_at_entry = 0;
static uint32_t sma2_irq_return_pc = 0;
static uint32_t sma2_irq_post_pc = 0;
static uint32_t sma2_irq_post_target = 0;
static uint32_t sma2_irq_overflow_seen = 0;
static uint64_t sma2_irq_cycle_baseline = 0;
static uint32_t sma2_irq_budget_expired = 0;
#endif

#if defined(SMA2_CART_AOT_BENCH)
static uint32_t sma2_bench_count_helpers = 0;
static uint32_t sma2_bench_tick_calls = 0;
static uint32_t sma2_bench_yield_calls = 0;
static uint64_t sma2_bench_tick_ns = 0;
static uint64_t sma2_bench_yield_ns = 0;
#endif

extern "C" void __real_runtime_tick(uint32_t cycles);
extern "C" void __wrap_runtime_tick(uint32_t cycles) {
#if defined(SMA2_CART_AOT_BENCH)
    const bool measure = sma2_bench_count_helpers != 0;
    const auto start = measure ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};
    if (measure) ++sma2_bench_tick_calls;
#endif
    const uint16_t if_before = sma2_event_trace_active && gbarecomp::active_bus()
        ? gbarecomp::active_bus()->io().read16(0x202u) : 0u;
    const uint32_t pc = sma2_event_trace_active ? (g_cpu.R[15] & ~1u) : 0u;
    __real_runtime_tick(cycles);
#if defined(SMA2_CART_AOT_IRQ_DIAG)
    if (sma2_irq_stop_active &&
        g_runtime_irq_entries > sma2_irq_stop_baseline &&
        g_irq_nest_depth == 0u) {
        if (!sma2_irq_seen) {
            sma2_irq_seen = 1u;
            sma2_irq_return_pc = g_cpu.R[15];
        } else if (sma2_irq_post_step_armed) {
            sma2_irq_post_pc = sma2_irq_post_target;
            sma2_irq_stop_pc = g_cpu.R[15];
            g_runtime_break_pc = sma2_irq_stop_pc;
            sma2_irq_post_step_armed = 0u;
        }
    }
    if (sma2_irq_trace_active && gbarecomp::active_bus()) {
        const uint16_t if_after = gbarecomp::active_bus()->io().read16(0x202u);
        if (if_after & (1u << 3)) sma2_irq_overflow_seen = 1u;
    }
#endif
    if (sma2_event_trace_active && gbarecomp::active_bus()) {
        const uint16_t if_after = gbarecomp::active_bus()->io().read16(0x202u);
        constexpr uint16_t kTimer0Irq = 1u << 3;
        if (!(if_before & kTimer0Irq) && (if_after & kTimer0Irq) &&
            sma2_event_trace_count < 8u) {
            auto& event = sma2_event_trace[sma2_event_trace_count++];
            event = {pc, if_before, if_after,
                     g_runtime_cycles - sma2_event_trace_origin};
        }
    }
#if defined(SMA2_CART_AOT_BENCH)
    if (measure) {
        sma2_bench_tick_ns += static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    }
#endif
}

#if defined(SMA2_CART_AOT_BENCH) || defined(SMA2_CART_AOT_IRQ_DIAG)
extern "C" bool __real_runtime_should_yield(void);
extern "C" bool __wrap_runtime_should_yield(void) {
#if defined(SMA2_CART_AOT_BENCH)
    const bool measure = sma2_bench_count_helpers != 0;
    const auto start = measure ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};
    if (measure) ++sma2_bench_yield_calls;
#endif
#if defined(SMA2_CART_AOT_IRQ_DIAG)
    if (sma2_irq_trace_active && g_irq_nest_depth != 0u &&
        sma2_irq_trace_count < 512u) {
        const uint32_t pc = (g_cpu.R[15] & ~1u) |
            ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u);
        auto& row = sma2_irq_trace[sma2_irq_trace_count++];
        row = {pc, g_cpu.cpsr, g_cpu.banked_spsr[ARM_BANK_IRQ]};
        if (!sma2_irq_entry_pc) sma2_irq_entry_pc = pc;
    }
    if (sma2_irq_stop_active && sma2_irq_seen && !sma2_irq_post_step_armed &&
        g_irq_nest_depth == 0u && g_runtime_irq_entries > sma2_irq_stop_baseline) {
        sma2_irq_post_target = g_cpu.R[15];
        sma2_irq_post_step_armed = 1u;
    }
    const bool budget_yield = sma2_irq_stop_active && g_irq_nest_depth == 0u &&
        g_runtime_cycles - sma2_irq_cycle_baseline > 2u * 280896u;
    if (budget_yield) sma2_irq_budget_expired = 1u;
#else
    const bool budget_yield = false;
#endif
    const bool result = __real_runtime_should_yield();
#if defined(SMA2_CART_AOT_BENCH)
    if (measure) {
        sma2_bench_yield_ns += static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    }
#endif
    return result || budget_yield;
}
#endif

#if defined(SMA2_CART_AOT_IRQ_DIAG)
extern "C" void __real_runtime_irq(uint32_t return_address);
extern "C" void __wrap_runtime_irq(uint32_t return_address) {
    if (sma2_irq_trace_active && g_irq_nest_depth == 0u) {
        // runtime_irq() itself sets g_cpu.R15=0x18 and dispatches that BIOS
        // vector before the first tick-visible handler PC (0x128).
        ++sma2_irq_vector_dispatches;
        sma2_irq_vector_pc = 0x18u;
        sma2_irq_return_arg = return_address;
        sma2_irq_cpsr_at_entry = g_cpu.cpsr;
        if (auto* bus = gbarecomp::active_bus()) {
            sma2_irq_if_at_entry = bus->io().read16(0x202u);
            sma2_irq_ie_at_entry = bus->io().read16(0x200u);
            sma2_irq_ime_at_entry = bus->io().read16(0x208u);
        }
    }
    __real_runtime_irq(return_address);
    if (sma2_irq_trace_active) sma2_irq_return_pc = g_cpu.R[15];
}
extern "C" void sma2_aot_irq_instruction_boundary(uint32_t pc, int thumb) {
    if (!sma2_irq_trace_active || g_irq_nest_depth == 0u ||
        sma2_irq_trace_count >= 512u) return;
    const uint32_t mode_pc = (pc & ~1u) | (thumb ? 1u : 0u);
    auto& row = sma2_irq_trace[sma2_irq_trace_count++];
    row = {mode_pc, g_cpu.cpsr, g_cpu.banked_spsr[ARM_BANK_IRQ]};
    if (!sma2_irq_entry_pc) sma2_irq_entry_pc = mode_pc;
}
#endif

namespace {
constexpr uint32_t kEntry = 0x0809DFBEu;
constexpr uint32_t kReturn = 0x080015FAu;
constexpr uint32_t kThumbBxLr = 0x4770u;
constexpr uint32_t kChainEntryA = 0x08007056u;
constexpr uint32_t kChainEntryB = 0x08007058u;
constexpr uint32_t kChainReturn = 0x080015FAu;
constexpr uint32_t kChainStack = 0x03007000u;
constexpr uint32_t kMemoryLoopEntry = 0x08005538u;
constexpr uint32_t kMemoryLoopSource = 0x02000000u;
constexpr uint32_t kMemoryLoopDestination = 0x03006000u;
constexpr uint16_t kMemoryLoopOpcodes[] = {
    0x7808u, 0x7018u, 0x3101u, 0x3301u,  // ldrb/strb; increment pointers
    0x1C10u, 0x3A01u, 0x2800u, 0xD1F7u,  // adds/subs/cmp/bne
    0x4770u                               // bx lr
};
constexpr uint16_t kChainOpcodes[] = {
    0x2000u, 0xB002u, 0xBC38u, 0x4698u, 0x46A1u,
    0x46AAu, 0xBCF0u, 0xBC02u, 0x4708u
};
uint32_t g_entries = 0;
uint32_t g_entry_pc = 0;
uint32_t g_entry_pcs[8]{};

void entry_hook(uint32_t pc) {
    if (g_entries < 8u) g_entry_pcs[g_entries] = pc;
    ++g_entries;
    g_entry_pc = pc;
}

uint64_t hash_bytes(const uint8_t* bytes, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint32_t read_le32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1u]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2u]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3u]) << 24);
}

void report_snapshot_difference(const char* tag,
                                const std::vector<uint8_t>& a,
                                const std::vector<uint8_t>& b) {
    size_t offset = 0;
    const size_t common = std::min(a.size(), b.size());
    while (offset < common && a[offset] == b[offset]) ++offset;
    if (offset == common && a.size() == b.size()) return;
    uint32_t section = 0;
    size_t payload = 52u;
    if (a.size() >= 52u) {
        const uint32_t count = read_le32(a, 48u);
        for (uint32_t i = 0; i < count && payload + 8u <= a.size(); ++i) {
            const uint32_t current = read_le32(a, payload);
            const uint32_t length = read_le32(a, payload + 4u);
            if (offset >= payload && offset < payload + 8u + length) {
                section = current;
                offset -= payload + 8u;
                break;
            }
            payload += 8u + length;
        }
    }
    std::fprintf(stderr,
        "%s snapshot_first_diff section=0x%08" PRIx32 " payload_offset=%zu"
        " values=%02x/%02x sizes=%zu/%zu\n",
        tag, section, offset, offset < a.size() ? a[payload + 8u + offset] : 0u,
        offset < b.size() ? b[payload + 8u + offset] : 0u, a.size(), b.size());
}

bool cpu_equal(const ArmCpuState& a, const ArmCpuState& b) {
    return std::memcmp(&a, &b, sizeof(ArmCpuState)) == 0;
}

ArmCpuState fixture_cpu() {
    ArmCpuState cpu{};
    for (uint32_t i = 0; i < 16; ++i)
        cpu.R[i] = 0x13570000u + i * 0x101u;
    for (uint32_t i = 0; i < ARM_BANK_COUNT; ++i) {
        cpu.banked_sp[i] = 0x03007000u + i * 0x20u;
        cpu.banked_lr[i] = 0x08001001u + i * 2u;
        cpu.banked_spsr[i] = 0xA0000010u + i;
    }
    for (uint32_t i = 0; i < 5; ++i) {
        cpu.r8_12_user[i] = 0x22000000u + i;
        cpu.r8_12_fiq[i] = 0x33000000u + i;
    }
    cpu.R[13] = 0x03007F00u;
    cpu.R[14] = kReturn | 1u;
    cpu.R[15] = kEntry;
    cpu.cpsr = 0xD0000000u | CPSR_I_BIT | CPSR_F_BIT |
               CPSR_T_BIT | 0x1Fu;
    return cpu;
}

struct RunResult {
    ArmCpuState cpu{};
    uint64_t cycles = 0;
    uint64_t iwram = 0;
    uint64_t ewram = 0;
    uint64_t vblank = 0;
    uint64_t irq_entries = 0;
    uint32_t return_depth = 0;
};

RunResult capture(uint64_t cycle_start, gba::GbaBus& bus) {
    gbarecomp::RuntimeArmContext context;
    gbarecomp::runtime_capture_arm_context(context);
    return {g_cpu, g_runtime_cycles - cycle_start,
            hash_bytes(bus.iwram_read_ptr(), 32u * 1024u),
            hash_bytes(bus.ewram_ptr(), 256u * 1024u),
            g_runtime_vblank_starts, context.irq_entries,
            runtime_call_stack_depth()};
}

void set_fixture_cpu() {
    g_cpu = fixture_cpu();
}

bool run_arm_reset_entry(gba::GbaBus& bus,
                         const gbarecomp::RuntimeArmContext& original,
                         const char* tag) {
    constexpr uint32_t kArmEntry = 0x08000000u;
    constexpr uint32_t kArmCallee = 0x080000C0u;
    if (!runtime_has_static_entry(kArmEntry, 0) ||
        !runtime_has_static_entry(kArmCallee, 0) ||
        bus.read32(kArmEntry) != 0xEA00002Eu) {
        std::fprintf(stderr,
            "%s status=INELIGIBLE contract=ARM_RESET_ENTRY reason=entry_or_opcode\n",
            tag);
        return false;
    }

    const uint32_t old_break_pc = g_runtime_break_pc;
    gbarecomp::RuntimeTimingContext timing_original{};
    gbarecomp::runtime_capture_timing_context(timing_original);
    const uint64_t iwram_before = hash_bytes(bus.iwram_read_ptr(), 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    g_runtime_break_pc = kArmCallee;

    g_cpu = {};
    g_cpu.R[13] = 0x03007F00u;
    g_cpu.R[15] = kArmEntry;
    g_cpu.cpsr = 0x1Fu;
    const uint64_t native_start = g_runtime_cycles;
    runtime_dispatch(kArmEntry);
    const RunResult native = capture(native_start, bus);

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    g_cpu = {};
    g_cpu.R[13] = 0x03007F00u;
    g_cpu.R[15] = kArmEntry;
    g_cpu.cpsr = 0x1Fu;
    const uint64_t interp_start = g_runtime_cycles;
    runtime_force_interp_step();
    const RunResult interp = capture(interp_start, bus);

    const bool state_match = cpu_equal(native.cpu, interp.cpu);
    const bool memory_match = native.iwram == iwram_before &&
        interp.iwram == iwram_before && native.ewram == ewram_before &&
        interp.ewram == ewram_before;
    const bool pass = native.cpu.R[15] == kArmCallee &&
        interp.cpu.R[15] == kArmCallee &&
        !(native.cpu.cpsr & CPSR_T_BIT) && !(interp.cpu.cpsr & CPSR_T_BIT) &&
        state_match && memory_match && native.cycles == interp.cycles &&
        native.return_depth == 0u && interp.return_depth == 0u;

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    g_runtime_break_pc = old_break_pc;
    std::fprintf(stderr,
        "%s status=%s contract=ARM_RESET_ENTRY entry=0x%08" PRIx32
        " callee=0x%08" PRIx32 " cycles_native=%llu cycles_interp=%llu"
        " cpu=%s memory=%s return_depth=%" PRIu32 "/%" PRIu32 "\n",
        tag, pass ? "PASS" : "MISMATCH", kArmEntry, kArmCallee,
        static_cast<unsigned long long>(native.cycles),
        static_cast<unsigned long long>(interp.cycles),
        state_match ? "MATCH" : "DIFF", memory_match ? "MATCH" : "DIFF",
        native.return_depth, interp.return_depth);
    return pass;
}

bool timing_equal(const gbarecomp::RuntimeTimingContext& a,
                  const gbarecomp::RuntimeTimingContext& b) {
    return a.cycles == b.cycles &&
           a.vblank_starts == b.vblank_starts &&
           a.yielded_vblank == b.yielded_vblank &&
           a.pending_cycles == b.pending_cycles &&
           a.event_budget == b.event_budget;
}

bool run_two_function_chain(gba::GbaBus& bus, gba::GbaPpu& ppu,
                            const gbarecomp::RuntimeArmContext& original,
                            const char* tag) {
    const bool entries_present = runtime_has_static_entry(kChainEntryA, 1) &&
                                 runtime_has_static_entry(kChainEntryB, 1);
    bool opcodes_match = true;
    for (uint32_t i = 0; i < sizeof(kChainOpcodes) / sizeof(kChainOpcodes[0]); ++i)
        opcodes_match = opcodes_match &&
            bus.read16(kChainEntryA + i * 2u) == kChainOpcodes[i];
    if (!entries_present || !opcodes_match) {
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=%s a=%d b=%d\n",
            tag, entries_present ? "rom_opcode_mismatch" : "missing_static_entry",
            runtime_has_static_entry(kChainEntryA, 1),
            runtime_has_static_entry(kChainEntryB, 1));
        return false;
    }

    auto* iwram = bus.iwram_write_ptr("sma2_corpus_dispatch_diag");
    uint32_t saved_words[8]{};
    std::memcpy(saved_words, iwram + 0x7008u, sizeof(saved_words));
    const uint32_t stack_words[8] = {
        0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u,
        0x9999AAAAu, 0xBBBBCCCCu, 0xDDDDEEEEu, kChainReturn | 1u
    };
    std::memcpy(iwram + 0x7008u, stack_words, sizeof(stack_words));

    gbarecomp::RuntimeArmContext context_original = original;
    const uint32_t old_break_pc = g_runtime_break_pc;
    g_runtime_break_pc = 0;

    auto event_headroom = [&]() {
        return std::min({ppu.cycles_until_next_event(),
                         bus.audio().cycles_until_next_sample(),
                         bus.io().cycles_until_next_timer_event(),
                         bus.io().cycles_until_next_sio_event()});
    };
    auto set_chain_cpu = [&]() {
        g_cpu = fixture_cpu();
        g_cpu.R[15] = kChainEntryA;
        g_cpu.R[13] = kChainStack;
        g_cpu.R[14] = kChainReturn | 1u;
        g_cpu.R[1] = 0xCAFEBABEu;
    };

    set_chain_cpu();
    uint32_t headroom = event_headroom();
    for (unsigned i = 0; headroom <= 128u && i < 4u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, headroom));
        headroom = event_headroom();
    }
    if (headroom <= 128u || bus.io().irq_pending() || bus.io().halted()) {
        gbarecomp::runtime_restore_arm_context(context_original);
        g_runtime_break_pc = old_break_pc;
        std::memcpy(iwram + 0x7008u, saved_words, sizeof(saved_words));
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=no_quiet_window headroom=%" PRIu32
            " irq=%d halted=%d\n", tag, headroom,
            bus.io().irq_pending() ? 1 : 0, bus.io().halted() ? 1 : 0);
        return false;
    }

    // Both executions start from this same scheduler state. Capture after any
    // quiet-window alignment so device state and the event horizon stay paired.
    gbarecomp::RuntimeTimingContext timing_original{};
    gbarecomp::runtime_capture_timing_context(timing_original);

    const uint64_t iwram_before = hash_bytes(iwram, 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    const uint64_t vblank_before = g_runtime_vblank_starts;
    g_entries = 0;
    g_entry_pc = 0;
    std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
    g_runtime_fn_entry_hook = entry_hook;
    runtime_call_push_return(kChainReturn);
    const uint64_t native_start = g_runtime_cycles;
    runtime_dispatch(kChainEntryA | 1u);
    const RunResult native = capture(native_start, bus);
    gbarecomp::RuntimeTimingContext native_timing{};
    gbarecomp::runtime_capture_timing_context(native_timing);
    const bool native_chain = g_entries == 2u &&
        g_entry_pcs[0] == kChainEntryA && g_entry_pcs[1] == kChainEntryB;

    gbarecomp::runtime_restore_arm_context(context_original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    set_chain_cpu();
    uint32_t interp_steps = 0;
    const uint64_t interp_start = g_runtime_cycles;
    while (g_cpu.R[15] != kChainReturn && interp_steps < 16u) {
        runtime_force_interp_step();
        ++interp_steps;
    }
    const RunResult interp = capture(interp_start, bus);
    gbarecomp::RuntimeTimingContext interp_timing{};
    gbarecomp::runtime_capture_timing_context(interp_timing);

    const bool state_match = cpu_equal(native.cpu, interp.cpu);
    const bool memory_match = native.iwram == iwram_before &&
        interp.iwram == iwram_before && native.ewram == ewram_before &&
        interp.ewram == ewram_before;
    const bool events_match = native.vblank == vblank_before &&
        interp.vblank == vblank_before &&
        native.irq_entries == context_original.irq_entries &&
        interp.irq_entries == context_original.irq_entries &&
        !bus.io().irq_pending() && !bus.io().halted();
    const bool exit_match = native.cpu.R[15] == kChainReturn &&
        interp.cpu.R[15] == kChainReturn &&
        (native.cpu.cpsr & CPSR_T_BIT) && (interp.cpu.cpsr & CPSR_T_BIT);
    const bool pass = native_chain && state_match && memory_match &&
        events_match && exit_match && native.cycles == interp.cycles &&
        interp_steps == 9u && native.return_depth == 0u &&
        interp.return_depth == 0u && timing_equal(native_timing, interp_timing);

    gbarecomp::runtime_restore_arm_context(context_original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    g_runtime_break_pc = old_break_pc;
    std::memcpy(iwram + 0x7008u, saved_words, sizeof(saved_words));
    std::fprintf(stderr,
        "%s status=%s contract=CHAIN entries=%" PRIu32
        " path=0x%08" PRIx32 ",0x%08" PRIx32
        " guest_insns_native=9 guest_insns_interp=%" PRIu32
        " exit_native=0x%08" PRIx32 " exit_interp=0x%08" PRIx32
        " cycles_native=%llu cycles_interp=%llu cpu=%s iwram=%s ewram=%s"
        " events=%s scheduler=%s return_depth=%" PRIu32 "/%" PRIu32 "\n",
        tag, pass ? "PASS" : "MISMATCH", g_entries, g_entry_pcs[0],
        g_entry_pcs[1], interp_steps, native.cpu.R[15], interp.cpu.R[15],
        static_cast<unsigned long long>(native.cycles),
        static_cast<unsigned long long>(interp.cycles),
        state_match ? "MATCH" : "DIFF", memory_match ? "MATCH" : "DIFF",
        native.ewram == interp.ewram ? "MATCH" : "DIFF",
        events_match ? "CLEAR" : "CHANGED",
        timing_equal(native_timing, interp_timing) ? "MATCH" : "DIFF",
        native.return_depth, interp.return_depth);
    return pass;
}

bool run_memory_branch_case(gba::GbaBus& bus, gba::GbaPpu& ppu,
                            const gbarecomp::RuntimeArmContext& original,
                            uint32_t initial_count) {
    constexpr const char* tag = "[sma2-cart-aot-memory-branch]";
    constexpr uint32_t kReturnPc = 0x080015FAu;
    constexpr uint32_t kLoopController = 0x08005540u;
    bool opcodes_match = true;
    uint32_t mismatch_pc = 0u;
    uint32_t mismatch_expected = 0u;
    uint32_t mismatch_actual = 0u;
    for (uint32_t i = 0; i < sizeof(kMemoryLoopOpcodes) / sizeof(kMemoryLoopOpcodes[0]); ++i) {
        const uint32_t pc = (i < 4u) ? kMemoryLoopEntry + i * 2u
                                     : kLoopController + (i - 4u) * 2u;
        const uint32_t actual = bus.read16(pc);
        if (actual != kMemoryLoopOpcodes[i] && opcodes_match) {
            mismatch_pc = pc;
            mismatch_expected = kMemoryLoopOpcodes[i];
            mismatch_actual = actual;
            opcodes_match = false;
        }
    }
    const bool entries_present = runtime_has_static_entry(kMemoryLoopEntry, 1) &&
        runtime_has_static_entry(kLoopController, 1);
    if (!entries_present || !opcodes_match) {
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=%s entry=0x%08" PRIx32
            " controller=0x%08" PRIx32 " mismatch_pc=0x%08" PRIx32
            " expected=%04" PRIx32 " actual=%04" PRIx32 "\n",
            tag, opcodes_match ? "missing_static_entry" : "rom_opcode_mismatch",
            kMemoryLoopEntry, kLoopController, mismatch_pc,
            mismatch_expected, mismatch_actual);
        return false;
    }

    auto* iwram = bus.iwram_write_ptr("sma2_corpus_dispatch_diag");
    const uint32_t destination_offset = kMemoryLoopDestination - 0x03000000u;
    auto* ewram = bus.ewram_ptr();
    const uint32_t source_offset = kMemoryLoopSource - 0x02000000u;
    const uint8_t saved_destination[2] = {
        iwram[destination_offset], iwram[destination_offset + 1u]};
    const uint8_t saved_source[2] = {ewram[source_offset], ewram[source_offset + 1u]};
    constexpr uint8_t source_bytes[2] = {0xA5u, 0x5Au};
    constexpr uint8_t destination_seed[2] = {0xCCu, 0xDDu};
    ewram[source_offset] = source_bytes[0];
    ewram[source_offset + 1u] = source_bytes[1];
    iwram[destination_offset] = destination_seed[0];
    iwram[destination_offset + 1u] = destination_seed[1];
    const uint32_t old_break_pc = g_runtime_break_pc;
    auto old_hook = g_runtime_fn_entry_hook;
    g_runtime_break_pc = 0;

    auto event_headroom = [&]() {
        return std::min({ppu.cycles_until_next_event(),
                         bus.audio().cycles_until_next_sample(),
                         bus.io().cycles_until_next_timer_event(),
                         bus.io().cycles_until_next_sio_event()});
    };
    auto restore_fixture_memory = [&]() {
        iwram[destination_offset] = saved_destination[0];
        iwram[destination_offset + 1u] = saved_destination[1];
        ewram[source_offset] = saved_source[0];
        ewram[source_offset + 1u] = saved_source[1];
    };
    auto restore_destination_seed = [&]() {
        iwram[destination_offset] = destination_seed[0];
        iwram[destination_offset + 1u] = destination_seed[1];
    };
    std::unique_ptr<uint8_t[]> native_iwram_snapshot(
        new (std::nothrow) uint8_t[32u * 1024u]);
    std::unique_ptr<uint8_t[]> native_ewram_snapshot(
        new (std::nothrow) uint8_t[256u * 1024u]);
    if (!native_iwram_snapshot || !native_ewram_snapshot) {
        g_runtime_break_pc = old_break_pc;
        g_runtime_fn_entry_hook = old_hook;
        restore_fixture_memory();
        std::fprintf(stderr, "%s status=INELIGIBLE reason=memory_snapshot_allocation\n", tag);
        return false;
    }
    auto initialize_cpu = [&]() {
        g_cpu = fixture_cpu();
        g_cpu.R[15] = kMemoryLoopEntry;
        g_cpu.R[14] = kReturnPc | 1u;
        g_cpu.R[1] = kMemoryLoopSource;
        g_cpu.R[2] = initial_count;
        g_cpu.R[3] = kMemoryLoopDestination;
    };

    initialize_cpu();
    uint32_t headroom = event_headroom();
    for (unsigned i = 0; headroom <= 128u && i < 4u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, headroom));
        headroom = event_headroom();
    }
    if (headroom <= 128u || bus.io().irq_pending() || bus.io().halted()) {
        gbarecomp::runtime_restore_arm_context(original);
        g_runtime_break_pc = old_break_pc;
        g_runtime_fn_entry_hook = old_hook;
        restore_fixture_memory();
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=no_quiet_window headroom=%" PRIu32
            " irq=%d halted=%d\n", tag, headroom,
            bus.io().irq_pending() ? 1 : 0, bus.io().halted() ? 1 : 0);
        return false;
    }

    gbarecomp::RuntimeTimingContext timing_original{};
    gbarecomp::runtime_capture_timing_context(timing_original);
    const uint64_t iwram_before = hash_bytes(iwram, 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    const uint64_t vblank_before = g_runtime_vblank_starts;

    g_entries = 0;
    std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
    g_runtime_fn_entry_hook = entry_hook;
    runtime_call_push_return(kReturnPc);
    const uint64_t native_start = g_runtime_cycles;
    const uint64_t native_miss_insns_start = gbarecomp::self_heal_interpreted_insns();
    const uint32_t native_miss_pcs_start = gbarecomp::self_heal_distinct_misses();
    runtime_dispatch(kMemoryLoopEntry | 1u);
    const RunResult native = capture(native_start, bus);
    const uint64_t native_fallback_insns =
        gbarecomp::self_heal_interpreted_insns() - native_miss_insns_start;
    const uint32_t native_fallback_pcs =
        gbarecomp::self_heal_distinct_misses() - native_miss_pcs_start;
    std::memcpy(native_iwram_snapshot.get(), iwram, 32u * 1024u);
    std::memcpy(native_ewram_snapshot.get(), ewram, 256u * 1024u);
    const uint8_t native_destination[2] = {
        iwram[destination_offset], iwram[destination_offset + 1u]};
    gbarecomp::RuntimeTimingContext native_timing{};
    gbarecomp::runtime_capture_timing_context(native_timing);
    const uint32_t expected_entries = initial_count == 0u ? 2u : 4u;
    const uint32_t expected_path[] = {
        kMemoryLoopEntry, kLoopController, kMemoryLoopEntry, kLoopController};
    bool entry_path_match = g_entries == expected_entries;
    for (uint32_t i = 0; entry_path_match && i < expected_entries; ++i)
        entry_path_match = g_entry_pcs[i] == expected_path[i];

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    restore_destination_seed();
    initialize_cpu();
    uint32_t interp_steps = 0;
    const uint64_t interp_start = g_runtime_cycles;
    while (g_cpu.R[15] != kReturnPc && interp_steps < 32u) {
        runtime_force_interp_step();
        ++interp_steps;
    }
    const RunResult interp = capture(interp_start, bus);
    const uint8_t interp_destination[2] = {
        iwram[destination_offset], iwram[destination_offset + 1u]};
    const bool exact_memory_match =
        std::memcmp(native_iwram_snapshot.get(), iwram, 32u * 1024u) == 0 &&
        std::memcmp(native_ewram_snapshot.get(), ewram, 256u * 1024u) == 0;
    gbarecomp::RuntimeTimingContext interp_timing{};
    gbarecomp::runtime_capture_timing_context(interp_timing);

    const uint32_t iterations = initial_count + 1u;
    const uint32_t expected_steps = iterations * 8u + 1u;
    const bool branch_observation = initial_count == 0u
        ? (native.cpu.R[3] == kMemoryLoopDestination + 1u &&
           native.cpu.R[2] == 0xFFFFFFFFu)
        : (native.cpu.R[3] == kMemoryLoopDestination + 2u &&
           native.cpu.R[2] == 0xFFFFFFFFu);
    const bool memory_match = exact_memory_match &&
        native.iwram == interp.iwram &&
        native.ewram == interp.ewram &&
        native.iwram != iwram_before && native.ewram == ewram_before &&
        native_destination[0] == interp_destination[0] &&
        native_destination[1] == interp_destination[1] &&
        native_destination[0] == source_bytes[0] &&
        (initial_count == 0u
            ? native_destination[1] == destination_seed[1]
            : native_destination[1] == source_bytes[1]);
    const bool events_match = native.vblank == vblank_before &&
        interp.vblank == vblank_before &&
        native.irq_entries == original.irq_entries &&
        interp.irq_entries == original.irq_entries &&
        !bus.io().irq_pending() && !bus.io().halted();
    const bool pass = entry_path_match && branch_observation &&
        native_fallback_insns == 0u && native_fallback_pcs == 0u &&
        cpu_equal(native.cpu, interp.cpu) && memory_match && events_match &&
        native.cpu.R[15] == kReturnPc && interp.cpu.R[15] == kReturnPc &&
        (native.cpu.cpsr & CPSR_T_BIT) && (interp.cpu.cpsr & CPSR_T_BIT) &&
        native.cycles == interp.cycles && interp_steps == expected_steps &&
        native.return_depth == 0u && interp.return_depth == 0u &&
        timing_equal(native_timing, interp_timing);

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    restore_fixture_memory();
    g_runtime_break_pc = old_break_pc;
    g_runtime_fn_entry_hook = old_hook;
    std::fprintf(stderr,
        "%s status=%s entry=0x%08" PRIx32 " mode=thumb initial_count=%" PRIu32
        " native_entries=%" PRIu32 " path=0x%08" PRIx32 ",0x%08" PRIx32
        ",0x%08" PRIx32 ",0x%08" PRIx32 " branch_taken=%s branch_not_taken=yes"
        " exit_native=0x%08" PRIx32 " exit_interp=0x%08" PRIx32
        " fallback_insns=%llu fallback_pcs=%" PRIu32
        " guest_insns_interp=%" PRIu32 " cycles_native=%llu cycles_interp=%llu"
        " cpu=%s iwram=%s ewram=%s write=%02" PRIx32 "/%02" PRIx32
        " events=%s scheduler=%s return_depth=%" PRIu32 "/%" PRIu32 "\n",
        tag, pass ? "PASS" : "MISMATCH", kMemoryLoopEntry, initial_count,
        g_entries, g_entry_pcs[0], g_entry_pcs[1], g_entry_pcs[2], g_entry_pcs[3],
        initial_count == 0u ? "no" : "yes",
        native.cpu.R[15], interp.cpu.R[15],
        static_cast<unsigned long long>(native_fallback_insns), native_fallback_pcs,
        interp_steps, static_cast<unsigned long long>(native.cycles),
        static_cast<unsigned long long>(interp.cycles),
        cpu_equal(native.cpu, interp.cpu) ? "MATCH" : "DIFF",
        native.iwram == interp.iwram ? "MATCH" : "DIFF",
        native.ewram == interp.ewram ? "MATCH" : "DIFF",
        static_cast<uint32_t>(native_destination[0]),
        static_cast<uint32_t>(native_destination[1]),
        events_match ? "CLEAR" : "CHANGED",
        timing_equal(native_timing, interp_timing) ? "MATCH" : "DIFF",
        native.return_depth, interp.return_depth);
    return pass;
}

// Native SWI transition differential: a generated Thumb function whose body is
// `swi #imm` (tail-calls runtime_swi, then `bx lr` at the next generated function)
// is entered natively and compared against the reference interpreter. Covers
// the HLE CpuFastSet/CpuSet block-copy/fill paths, which write guest memory and
// charge SWI cycles inside the runtime rather than in generated code.
struct SwiCase {
    const char* name;
    uint32_t entry;
    uint16_t opcode;
    uint32_t r2;          // SWI length/flags argument
    uint32_t bytes;       // destination bytes expected to change
    bool fill;
};

bool run_swi_case(gba::GbaBus& bus, gba::GbaPpu& ppu,
                  const gbarecomp::RuntimeArmContext& original, const SwiCase& c) {
    constexpr const char* tag = "[sma2-cart-aot-swi]";
    constexpr uint32_t kReturnPc = 0x080015FAu;
    constexpr uint32_t kSrc = 0x0203F000u;
    constexpr uint32_t kDst = 0x03006000u;
    constexpr uint32_t kRegion = 256u;
    auto* iwram = bus.iwram_write_ptr("sma2_corpus_dispatch_diag");
    auto* ewram = bus.ewram_ptr();
    const uint32_t src_off = kSrc - 0x02000000u;
    const uint32_t dst_off = kDst - 0x03000000u;
    if (!runtime_has_static_entry(c.entry, 1) || bus.read16(c.entry) != c.opcode) {
        std::fprintf(stderr, "%s case=%s status=INELIGIBLE reason=%s entry=0x%08" PRIx32 "\n",
            tag, c.name, runtime_has_static_entry(c.entry, 1) ? "rom_opcode_mismatch"
                                                              : "missing_static_entry", c.entry);
        return false;
    }
    uint8_t saved_src[kRegion], saved_dst[kRegion];
    std::memcpy(saved_src, ewram + src_off, kRegion);
    std::memcpy(saved_dst, iwram + dst_off, kRegion);
    uint8_t src_seed[kRegion], dst_seed[kRegion];
    for (uint32_t i = 0; i < kRegion; ++i) {
        src_seed[i] = static_cast<uint8_t>(0x31u + i * 7u);
        dst_seed[i] = static_cast<uint8_t>(0xC0u ^ i);
    }
    auto seed = [&]() {
        std::memcpy(ewram + src_off, src_seed, kRegion);
        std::memcpy(iwram + dst_off, dst_seed, kRegion);
    };
    auto restore_memory = [&]() {
        std::memcpy(ewram + src_off, saved_src, kRegion);
        std::memcpy(iwram + dst_off, saved_dst, kRegion);
    };
    const uint32_t old_break_pc = g_runtime_break_pc;
    auto old_hook = g_runtime_fn_entry_hook;
    g_runtime_break_pc = 0;
    auto event_headroom = [&]() {
        return std::min({ppu.cycles_until_next_event(),
                         bus.audio().cycles_until_next_sample(),
                         bus.io().cycles_until_next_timer_event(),
                         bus.io().cycles_until_next_sio_event()});
    };
    auto initialize_cpu = [&]() {
        g_cpu = fixture_cpu();
        g_cpu.R[15] = c.entry;
        g_cpu.R[14] = kReturnPc | 1u;
        g_cpu.R[0] = kSrc;
        g_cpu.R[1] = kDst;
        g_cpu.R[2] = c.r2;
    };
    auto finish = [&]() {
        gbarecomp::runtime_restore_arm_context(original);
        restore_memory();
        g_runtime_break_pc = old_break_pc;
        g_runtime_fn_entry_hook = old_hook;
    };
    std::unique_ptr<uint8_t[]> native_iwram(new (std::nothrow) uint8_t[32u * 1024u]);
    std::unique_ptr<uint8_t[]> native_ewram(new (std::nothrow) uint8_t[256u * 1024u]);
    if (!native_iwram || !native_ewram) {
        finish();
        std::fprintf(stderr, "%s case=%s status=INELIGIBLE reason=memory_snapshot_allocation\n",
                     tag, c.name);
        return false;
    }
    initialize_cpu();
    uint32_t headroom = event_headroom();
    for (unsigned i = 0; headroom <= 160u && i < 8u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, headroom));
        headroom = event_headroom();
    }
    if (headroom <= 160u || bus.io().irq_pending() || bus.io().halted()) {
        finish();
        std::fprintf(stderr,
            "%s case=%s status=INELIGIBLE reason=no_quiet_window headroom=%" PRIu32 "\n",
            tag, c.name, headroom);
        return false;
    }

    seed();
    gbarecomp::RuntimeTimingContext timing_original{};
    gbarecomp::runtime_capture_timing_context(timing_original);
    const uint64_t vblank_before = g_runtime_vblank_starts;
    g_entries = 0;
    std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
    g_runtime_fn_entry_hook = entry_hook;
    runtime_call_push_return(kReturnPc);
    const uint64_t native_start = g_runtime_cycles;
    const uint64_t miss_insns_start = gbarecomp::self_heal_interpreted_insns();
    const uint32_t miss_pcs_start = gbarecomp::self_heal_distinct_misses();
    runtime_dispatch(c.entry | 1u);
    // GBARECOMP_TAIL_SWI returns to the dispatch caller after runtime_swi; the
    // real runtime loop then re-dispatches from R15. Mirror that here and count
    // the continuations so the SWI exit boundary is explicit.
    uint32_t continuations = 0;
    while (g_cpu.R[15] != kReturnPc && continuations < 8u) {
        runtime_dispatch(g_cpu.R[15] | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u));
        ++continuations;
    }
    const RunResult native = capture(native_start, bus);
    const uint64_t fallback_insns = gbarecomp::self_heal_interpreted_insns() - miss_insns_start;
    const uint32_t fallback_pcs = gbarecomp::self_heal_distinct_misses() - miss_pcs_start;
    std::memcpy(native_iwram.get(), iwram, 32u * 1024u);
    std::memcpy(native_ewram.get(), ewram, 256u * 1024u);
    gbarecomp::RuntimeTimingContext native_timing{};
    gbarecomp::runtime_capture_timing_context(native_timing);
    const uint32_t native_entries = g_entries;
    const uint32_t first_entry = g_entry_pcs[0];
    const uint32_t second_entry = g_entry_pcs[1];

    // Expected destination content, derived independently of either engine.
    uint8_t expected[kRegion];
    std::memcpy(expected, dst_seed, kRegion);
    for (uint32_t i = 0; i < c.bytes; ++i)
        expected[i] = c.fill ? src_seed[i % 4u] : src_seed[i];
    const bool expected_match = std::memcmp(native_iwram.get() + dst_off, expected, kRegion) == 0;

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    seed();
    initialize_cpu();
    uint32_t interp_steps = 0;
    const uint64_t interp_start = g_runtime_cycles;
    while (g_cpu.R[15] != kReturnPc && interp_steps < 64u) {
        runtime_force_interp_step();
        ++interp_steps;
    }
    const RunResult interp = capture(interp_start, bus);
    const bool exact_memory = std::memcmp(native_iwram.get(), iwram, 32u * 1024u) == 0 &&
                              std::memcmp(native_ewram.get(), ewram, 256u * 1024u) == 0;
    const bool interp_expected = std::memcmp(iwram + dst_off, expected, kRegion) == 0;
    gbarecomp::RuntimeTimingContext interp_timing{};
    gbarecomp::runtime_capture_timing_context(interp_timing);

    const bool events_match = native.vblank == vblank_before && interp.vblank == vblank_before &&
        native.irq_entries == original.irq_entries &&
        interp.irq_entries == original.irq_entries &&
        !bus.io().irq_pending() && !bus.io().halted();
    const bool pass = native_entries >= 1u && first_entry == c.entry && continuations < 8u &&
        fallback_insns == 0u && fallback_pcs == 0u &&
        cpu_equal(native.cpu, interp.cpu) && exact_memory &&
        native.iwram == interp.iwram && native.ewram == interp.ewram &&
        expected_match && interp_expected && events_match &&
        native.cpu.R[15] == kReturnPc && interp.cpu.R[15] == kReturnPc &&
        native.cycles == interp.cycles && interp_steps < 64u &&
        native.return_depth == 0u && interp.return_depth == 0u &&
        timing_equal(native_timing, interp_timing);

    finish();
    std::fprintf(stderr,
        "%s case=%s status=%s entry=0x%08" PRIx32 " swi_opcode=%04" PRIx32
        " r2=0x%08" PRIx32 " native_entries=%" PRIu32 " continuations=%" PRIu32 " path=0x%08" PRIx32
        ",0x%08" PRIx32 " exit_native=0x%08" PRIx32 " exit_interp=0x%08" PRIx32
        " fallback_insns=%llu fallback_pcs=%" PRIu32 " interp_steps=%" PRIu32
        " cycles_native=%llu cycles_interp=%llu cpu=%s iwram=%s ewram=%s"
        " expected_dst=%s/%s events=%s scheduler=%s return_depth=%" PRIu32 "/%" PRIu32 "\n",
        tag, c.name, pass ? "PASS" : "MISMATCH", c.entry, static_cast<uint32_t>(c.opcode),
        c.r2, native_entries, continuations, first_entry, second_entry, native.cpu.R[15], interp.cpu.R[15],
        static_cast<unsigned long long>(fallback_insns), fallback_pcs, interp_steps,
        static_cast<unsigned long long>(native.cycles),
        static_cast<unsigned long long>(interp.cycles),
        cpu_equal(native.cpu, interp.cpu) ? "MATCH" : "DIFF",
        native.iwram == interp.iwram && exact_memory ? "MATCH" : "DIFF",
        native.ewram == interp.ewram && exact_memory ? "MATCH" : "DIFF",
        expected_match ? "OK" : "BAD", interp_expected ? "OK" : "BAD",
        events_match ? "CLEAR" : "CHANGED",
        timing_equal(native_timing, interp_timing) ? "MATCH" : "DIFF",
        native.return_depth, interp.return_depth);
    return pass;
}

bool run_fallback_reentry(gba::GbaBus& bus, gba::GbaPpu& ppu,
                          const gbarecomp::RuntimeArmContext& original,
                          const char* tag) {
    constexpr uint32_t entry_a = 0x08007056u;
    constexpr uint32_t entry_b = 0x08007058u;
    constexpr uint32_t reentry = 0x080015ECu;
    constexpr uint32_t miss = 0x03006000u;
    constexpr uint32_t stack = 0x03007000u;
    constexpr uint16_t miss_opcode = 0x4770u; // Thumb BX LR in seeded IWRAM.
    constexpr uint16_t entry_a_opcode = 0x2000u;
    constexpr uint16_t entry_b_opcode = 0xB002u;

    if (!runtime_has_static_entry(entry_a, 1) ||
        !runtime_has_static_entry(entry_b, 1) ||
        !runtime_has_static_entry(reentry, 1) ||
        runtime_has_static_entry(miss, 1) ||
        bus.read16(entry_a) != entry_a_opcode ||
        bus.read16(entry_b) != entry_b_opcode || gbarecomp::overlay_enabled()) {
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=dispatch_or_fixture_guard\n", tag);
        return false;
    }

    auto* iwram = bus.iwram_write_ptr("sma2_corpus_dispatch_diag");
    uint16_t saved_miss_opcode = 0;
    std::memcpy(&saved_miss_opcode, iwram + 0x6000u,
                sizeof(saved_miss_opcode));
    std::memcpy(iwram + 0x6000u, &miss_opcode, sizeof(miss_opcode));
    uint32_t saved_stack[8]{};
    const uint32_t stack_seed[8] = {
        0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u,
        0x9999AAAAu, 0xBBBBCCCCu, 0xDDDDEEEEu, miss | 1u
    };
    std::memcpy(saved_stack, iwram + 0x7008u, sizeof(saved_stack));
    std::memcpy(iwram + 0x7008u, stack_seed, sizeof(stack_seed));

    const gbarecomp::RuntimeArmContext context_original = original;
    const uint32_t old_break_pc = g_runtime_break_pc;
    auto event_headroom = [&]() {
        return std::min({ppu.cycles_until_next_event(),
                         bus.audio().cycles_until_next_sample(),
                         bus.io().cycles_until_next_timer_event(),
                         bus.io().cycles_until_next_sio_event()});
    };
    auto set_cpu = [&]() {
        g_cpu = fixture_cpu();
        g_cpu.R[15] = entry_a;
        g_cpu.R[13] = stack;
        g_cpu.R[14] = reentry | 1u; // Top-level bridge stop PC.
    };
    set_cpu();
    uint32_t headroom = event_headroom();
    for (unsigned i = 0; headroom <= 128u && i < 4u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, headroom));
        headroom = event_headroom();
    }
    if (headroom <= 128u || bus.io().irq_pending() || bus.io().halted()) {
        gbarecomp::runtime_restore_arm_context(context_original);
        g_runtime_break_pc = old_break_pc;
        std::memcpy(iwram + 0x6000u, &saved_miss_opcode,
                    sizeof(saved_miss_opcode));
        std::memcpy(iwram + 0x7008u, saved_stack, sizeof(saved_stack));
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=no_quiet_window headroom=%" PRIu32
            " irq=%d halted=%d\n", tag, headroom,
            bus.io().irq_pending() ? 1 : 0, bus.io().halted() ? 1 : 0);
        return false;
    }

    gbarecomp::RuntimeTimingContext timing_original{};
    gbarecomp::runtime_capture_timing_context(timing_original);
    const uint64_t iwram_before = hash_bytes(iwram, 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    const uint64_t vblank_before = g_runtime_vblank_starts;
    const uint64_t miss_insns_before = gbarecomp::self_heal_interpreted_insns();
    const uint32_t miss_pcs_before = gbarecomp::self_heal_distinct_misses();

    g_runtime_break_pc = reentry;
    g_entries = 0;
    g_entry_pc = 0;
    std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
    g_runtime_fn_entry_hook = entry_hook;
    const uint64_t native_start = g_runtime_cycles;
    runtime_dispatch(entry_a | 1u);
    const uint32_t native_pc_before_reentry = g_cpu.R[15];
    const uint64_t miss_insns =
        gbarecomp::self_heal_interpreted_insns() - miss_insns_before;
    const uint32_t miss_pcs =
        gbarecomp::self_heal_distinct_misses() - miss_pcs_before;
    // This is the ordinary outer-dispatch boundary after the interpreter miss
    // returns at 0x080015EC. Enter that exact static row once; its prologue
    // observes g_runtime_break_pc and yields before executing the next guest op.
    if (native_pc_before_reentry == reentry)
        runtime_dispatch(reentry | 1u);
    const RunResult native = capture(native_start, bus);
    gbarecomp::RuntimeTimingContext native_timing{};
    gbarecomp::runtime_capture_timing_context(native_timing);
    const bool native_entries = g_entries == 3u &&
        g_entry_pcs[0] == entry_a && g_entry_pcs[1] == entry_b &&
        g_entry_pcs[2] == reentry;

    gbarecomp::runtime_restore_arm_context(context_original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    std::memcpy(iwram + 0x7008u, stack_seed, sizeof(stack_seed));
    set_cpu();
    g_runtime_break_pc = reentry;
    uint32_t interp_steps = 0;
    const uint64_t interp_start = g_runtime_cycles;
    while (g_cpu.R[15] != reentry && interp_steps < 16u) {
        runtime_force_interp_step();
        ++interp_steps;
    }
    const RunResult interp = capture(interp_start, bus);
    gbarecomp::RuntimeTimingContext interp_timing{};
    gbarecomp::runtime_capture_timing_context(interp_timing);

    const bool state_match = cpu_equal(native.cpu, interp.cpu);
    const bool memory_match = native.iwram == iwram_before &&
        interp.iwram == iwram_before && native.ewram == ewram_before &&
        interp.ewram == ewram_before;
    const bool events_match = native.vblank == vblank_before &&
        interp.vblank == vblank_before &&
        native.irq_entries == context_original.irq_entries &&
        interp.irq_entries == context_original.irq_entries &&
        !bus.io().irq_pending() && !bus.io().halted();
    const bool pass = native_entries && native_pc_before_reentry == reentry &&
        miss_pcs == 1u && miss_insns == 1u && interp_steps == 10u &&
        native.cpu.R[15] == reentry && interp.cpu.R[15] == reentry &&
        (native.cpu.cpsr & CPSR_T_BIT) && (interp.cpu.cpsr & CPSR_T_BIT) &&
        state_match && memory_match && events_match &&
        native.cycles == interp.cycles && native.return_depth == 0u &&
        interp.return_depth == 0u && timing_equal(native_timing, interp_timing);

    gbarecomp::runtime_restore_arm_context(context_original);
    gbarecomp::runtime_restore_timing_context(timing_original);
    g_runtime_break_pc = old_break_pc;
    std::memcpy(iwram + 0x6000u, &saved_miss_opcode,
                sizeof(saved_miss_opcode));
    std::memcpy(iwram + 0x7008u, saved_stack, sizeof(saved_stack));
    std::fprintf(stderr,
        "%s status=%s contract=FALLBACK_REENTRY entries=%" PRIu32
        " path=0x%08" PRIx32 "->0x%08" PRIx32 "->0x%08" PRIx32
        " fallback_misses=%" PRIu32 " fallback_insns=%llu"
        " guest_insns_native=10 guest_insns_interp=%" PRIu32
        " exit_native=0x%08" PRIx32 " exit_interp=0x%08" PRIx32
        " cycles_native=%llu cycles_interp=%llu cpu=%s iwram=%s ewram=%s"
        " events=%s scheduler=%s return_depth=%" PRIu32 "/%" PRIu32 "\n",
        tag, pass ? "PASS" : "MISMATCH", g_entries, entry_a, miss, reentry,
        miss_pcs, static_cast<unsigned long long>(miss_insns), interp_steps,
        native.cpu.R[15], interp.cpu.R[15],
        static_cast<unsigned long long>(native.cycles),
        static_cast<unsigned long long>(interp.cycles),
        state_match ? "MATCH" : "DIFF", memory_match ? "MATCH" : "DIFF",
        native.ewram == interp.ewram ? "MATCH" : "DIFF",
        events_match ? "CLEAR" : "CHANGED",
        timing_equal(native_timing, interp_timing) ? "MATCH" : "DIFF",
        native.return_depth, interp.return_depth);
    return pass;
}

#if defined(SMA2_CART_AOT_BENCH)
bool run_chain_benchmark(gba::GbaBus& bus, gba::GbaPpu& ppu,
                         const gbarecomp::RuntimeArmContext& original) {
    constexpr uint32_t repetitions = 128u;
    constexpr const char* tag = "[sma2-cart-aot-bench]";
    if (!runtime_has_static_entry(kChainEntryA, 1) ||
        !runtime_has_static_entry(kChainEntryB, 1) ||
        runtime_call_stack_depth() != 0u || bus.io().irq_pending() ||
        bus.io().halted()) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=fixture_guard\n", tag);
        return false;
    }
    uint32_t saved_stack[8]{};
    std::memcpy(saved_stack, bus.iwram_read_ptr() + 0x7008u, sizeof(saved_stack));
    const uint32_t stack_seed[8] = {
        0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u,
        0x9999AAAAu, 0xBBBBCCCCu, 0xDDDDEEEEu, kChainReturn | 1u
    };
    std::memcpy(bus.iwram_write_ptr("sma2_corpus_dispatch_diag") + 0x7008u, stack_seed, sizeof(stack_seed));

    gbarecomp::RuntimeTimingContext timing_base{};
    gbarecomp::runtime_capture_timing_context(timing_base);
    const uint32_t quiet_window = std::min({
        ppu.cycles_until_next_event(), bus.audio().cycles_until_next_sample(),
        bus.io().cycles_until_next_timer_event(), bus.io().cycles_until_next_sio_event()});
    if (quiet_window <= 128u) {
        std::memcpy(bus.iwram_write_ptr("sma2_corpus_dispatch_diag") + 0x7008u, saved_stack, sizeof(saved_stack));
        std::fprintf(stderr, "%s status=INELIGIBLE reason=short_quiet_window cycles=%" PRIu32 "\n",
                     tag, quiet_window);
        return false;
    }

    const uint64_t iwram_before = hash_bytes(bus.iwram_read_ptr(), 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    uint32_t native_entries = 0;
    uint64_t native_cycles = 0;
    uint32_t native_ticks = 0;
    uint32_t native_yields = 0;
    g_entries = 0;
    g_runtime_fn_entry_hook = entry_hook;
    sma2_bench_tick_calls = sma2_bench_yield_calls = 0;
    sma2_bench_count_helpers = 1;
    const auto native_start = std::chrono::steady_clock::now();
    bool native_ok = true;
    for (uint32_t i = 0; i < repetitions; ++i) {
        gbarecomp::runtime_restore_arm_context(original);
        gbarecomp::runtime_restore_timing_context(timing_base);
        g_runtime_fn_entry_hook = entry_hook;
        g_entries = 0;
        g_cpu = fixture_cpu();
        g_cpu.R[15] = kChainEntryA;
        g_cpu.R[13] = kChainStack;
        g_cpu.R[14] = kChainReturn | 1u;
        g_cpu.R[1] = 0xCAFEBABEu;
        const uint64_t cycle_start = g_runtime_cycles;
        runtime_call_push_return(kChainReturn);
        runtime_dispatch(kChainEntryA | 1u);
        native_cycles += g_runtime_cycles - cycle_start;
        native_entries += g_entries;
        native_ok = native_ok && g_cpu.R[15] == kChainReturn &&
            g_entries == 2u && g_entry_pcs[0] == kChainEntryA &&
            g_entry_pcs[1] == kChainEntryB &&
            runtime_call_stack_depth() == 0u && g_runtime_cycles - cycle_start == 52u;
    }
    const auto native_end = std::chrono::steady_clock::now();
    sma2_bench_count_helpers = 0;
    native_ticks = sma2_bench_tick_calls;
    native_yields = sma2_bench_yield_calls;
    const ArmCpuState native_cpu = g_cpu;

    uint64_t interp_cycles = 0;
    uint32_t interp_steps = 0;
    sma2_bench_tick_calls = sma2_bench_yield_calls = 0;
    sma2_bench_count_helpers = 1;
    const auto interp_start = std::chrono::steady_clock::now();
    bool interp_ok = true;
    for (uint32_t i = 0; i < repetitions; ++i) {
        gbarecomp::runtime_restore_arm_context(original);
        gbarecomp::runtime_restore_timing_context(timing_base);
        g_cpu = fixture_cpu();
        g_cpu.R[15] = kChainEntryA;
        g_cpu.R[13] = kChainStack;
        g_cpu.R[14] = kChainReturn | 1u;
        g_cpu.R[1] = 0xCAFEBABEu;
        const uint64_t cycle_start = g_runtime_cycles;
        uint32_t steps = 0;
        while (g_cpu.R[15] != kChainReturn && steps < 16u) {
            runtime_force_interp_step();
            ++steps;
        }
        interp_cycles += g_runtime_cycles - cycle_start;
        interp_steps += steps;
        interp_ok = interp_ok && g_cpu.R[15] == kChainReturn &&
            steps == 9u && g_runtime_cycles - cycle_start == 52u;
    }
    const auto interp_end = std::chrono::steady_clock::now();
    sma2_bench_count_helpers = 0;
    const uint32_t interp_ticks = sma2_bench_tick_calls;
    const uint32_t interp_yields = sma2_bench_yield_calls;
    const ArmCpuState interp_cpu = g_cpu;

    const bool memory_unchanged = iwram_before == hash_bytes(bus.iwram_read_ptr(), 32u * 1024u) &&
        ewram_before == hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    const bool pass = native_ok && interp_ok && cpu_equal(native_cpu, interp_cpu) &&
        native_entries == repetitions * 2u && interp_steps == repetitions * 9u &&
        native_cycles == interp_cycles && native_ticks != 0u && interp_ticks != 0u &&
        memory_unchanged &&
        !bus.io().irq_pending() && !bus.io().halted();

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_base);
    g_runtime_fn_entry_hook = nullptr;
    std::memcpy(bus.iwram_write_ptr("sma2_corpus_dispatch_diag") + 0x7008u, saved_stack, sizeof(saved_stack));
    const auto native_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(native_end - native_start).count();
    const auto interp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(interp_end - interp_start).count();
    std::fprintf(stderr,
        "%s status=%s repetitions=%" PRIu32 " guest_insns=%" PRIu32
        " guest_cycles=%llu native_entries=%" PRIu32 " fallback=0"
        " native_ns=%lld interp_ns=%lld native_ns_per_insn=%lld"
        " interp_ns_per_insn=%lld native_tick_calls=%" PRIu32
        " interp_tick_calls=%" PRIu32 " native_yield_calls=%" PRIu32
        " interp_yield_calls=%" PRIu32 " cpu=%s memory=%s events=CLEAR"
        " note=paired_replay_harness_included\n",
        tag, pass ? "PASS" : "MISMATCH", repetitions,
        repetitions * 9u, static_cast<unsigned long long>(native_cycles),
        native_entries, static_cast<long long>(native_ns),
        static_cast<long long>(interp_ns),
        static_cast<long long>(native_ns / (repetitions * 9u)),
        static_cast<long long>(interp_ns / (repetitions * 9u)),
        native_ticks, interp_ticks,
        native_yields, interp_yields,
        cpu_equal(native_cpu, interp_cpu) ? "MATCH" : "DIFF",
        memory_unchanged ? "MATCH" : "DIFF");
    return pass;
}

bool run_memory_branch_benchmark(gba::GbaBus& bus, gba::GbaPpu& ppu,
                                 const gbarecomp::RuntimeArmContext& original) {
    constexpr uint32_t repetitions = 4u;
    constexpr uint32_t guest_insns_per_run = 17u;
    constexpr uint32_t guest_cycles_per_run = 85u;
    constexpr const char* tag = "[sma2-cart-aot-memory-bench]";
    if (!runtime_has_static_entry(kMemoryLoopEntry, 1) ||
        !runtime_has_static_entry(0x08005540u, 1) ||
        runtime_call_stack_depth() != 0u || bus.io().irq_pending() || bus.io().halted()) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=fixture_guard\n", tag);
        return false;
    }

    auto* iwram = bus.iwram_write_ptr("sma2_corpus_dispatch_diag");
    auto* ewram = bus.ewram_ptr();
    const uint32_t destination_offset = kMemoryLoopDestination - 0x03000000u;
    const uint32_t source_offset = kMemoryLoopSource - 0x02000000u;
    const uint8_t saved_destination[2] = {
        iwram[destination_offset], iwram[destination_offset + 1u]};
    const uint8_t saved_source[2] = {ewram[source_offset], ewram[source_offset + 1u]};
    constexpr uint8_t source_seed[2] = {0xA5u, 0x5Au};
    constexpr uint8_t destination_seed[2] = {0xCCu, 0xDDu};
    ewram[source_offset] = source_seed[0];
    ewram[source_offset + 1u] = source_seed[1];
    auto seed_destination = [&]() {
        iwram[destination_offset] = destination_seed[0];
        iwram[destination_offset + 1u] = destination_seed[1];
    };
    auto restore_memory = [&]() {
        iwram[destination_offset] = saved_destination[0];
        iwram[destination_offset + 1u] = saved_destination[1];
        ewram[source_offset] = saved_source[0];
        ewram[source_offset + 1u] = saved_source[1];
    };
    auto set_cpu = []() {
        g_cpu = fixture_cpu();
        g_cpu.R[15] = kMemoryLoopEntry;
        g_cpu.R[14] = 0x080015FBu;
        g_cpu.R[1] = kMemoryLoopSource;
        g_cpu.R[2] = 1u;
        g_cpu.R[3] = kMemoryLoopDestination;
    };
    auto headroom_now = [&]() {
        return std::min({ppu.cycles_until_next_event(),
                         bus.audio().cycles_until_next_sample(),
                         bus.io().cycles_until_next_timer_event(),
                         bus.io().cycles_until_next_sio_event()});
    };

    set_cpu();
    uint32_t headroom = headroom_now();
    constexpr uint32_t kRequiredHeadroom = repetitions * guest_cycles_per_run + 24u;
    for (unsigned i = 0; headroom <= kRequiredHeadroom && i < 8u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, headroom));
        headroom = headroom_now();
    }
    if (headroom <= kRequiredHeadroom) {
        gbarecomp::runtime_restore_arm_context(original);
        restore_memory();
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=short_quiet_window headroom=%" PRIu32
            " required=%" PRIu32 "\n", tag, headroom, kRequiredHeadroom);
        return false;
    }

    gbarecomp::RuntimeTimingContext timing_base{};
    gbarecomp::runtime_capture_timing_context(timing_base);
    const uint64_t iwram_before = hash_bytes(iwram, 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(ewram, 256u * 1024u);
    const uint64_t vblank_before = g_runtime_vblank_starts;
    const uint64_t misses_before = gbarecomp::self_heal_interpreted_insns();
    uint32_t native_entries = 0u;
    uint32_t native_ticks = 0u;
    uint32_t native_yields = 0u;
    uint64_t native_tick_ns = 0u;
    uint64_t native_yield_ns = 0u;
    uint64_t native_cycles = 0u;
    bool native_ok = true;
    sma2_bench_tick_calls = sma2_bench_yield_calls = 0u;
    sma2_bench_tick_ns = sma2_bench_yield_ns = 0u;
    sma2_bench_count_helpers = 1u;
    const auto native_start = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < repetitions; ++i) {
        gbarecomp::runtime_restore_arm_context(original);
        gbarecomp::runtime_restore_timing_context(timing_base);
        seed_destination();
        set_cpu();
        g_entries = 0u;
        std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
        g_runtime_fn_entry_hook = entry_hook;
        const uint64_t cycle_start = g_runtime_cycles;
        runtime_call_push_return(0x080015FAu);
        runtime_dispatch(kMemoryLoopEntry | 1u);
        native_cycles += g_runtime_cycles - cycle_start;
        native_entries += g_entries;
        native_ok = native_ok && g_cpu.R[15] == 0x080015FAu &&
            g_entries == 4u && g_entry_pcs[0] == kMemoryLoopEntry &&
            g_entry_pcs[1] == 0x08005540u &&
            g_entry_pcs[2] == kMemoryLoopEntry &&
            g_entry_pcs[3] == 0x08005540u &&
            runtime_call_stack_depth() == 0u &&
            g_runtime_cycles - cycle_start == guest_cycles_per_run;
    }
    const auto native_end = std::chrono::steady_clock::now();
    sma2_bench_count_helpers = 0u;
    native_ticks = sma2_bench_tick_calls;
    native_yields = sma2_bench_yield_calls;
    native_tick_ns = sma2_bench_tick_ns;
    native_yield_ns = sma2_bench_yield_ns;
    const ArmCpuState native_cpu = g_cpu;
    const uint64_t native_iwram = hash_bytes(iwram, 32u * 1024u);
    const uint64_t native_ewram = hash_bytes(ewram, 256u * 1024u);

    uint32_t interp_ticks = 0u;
    uint32_t interp_yields = 0u;
    uint32_t interp_steps = 0u;
    uint64_t interp_cycles = 0u;
    bool interp_ok = true;
    sma2_bench_tick_calls = sma2_bench_yield_calls = 0u;
    sma2_bench_tick_ns = sma2_bench_yield_ns = 0u;
    sma2_bench_count_helpers = 1u;
    const auto interp_start = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < repetitions; ++i) {
        gbarecomp::runtime_restore_arm_context(original);
        gbarecomp::runtime_restore_timing_context(timing_base);
        seed_destination();
        set_cpu();
        const uint64_t cycle_start = g_runtime_cycles;
        uint32_t steps = 0u;
        while (g_cpu.R[15] != 0x080015FAu && steps < 32u) {
            runtime_force_interp_step();
            ++steps;
        }
        interp_steps += steps;
        interp_cycles += g_runtime_cycles - cycle_start;
        interp_ok = interp_ok && g_cpu.R[15] == 0x080015FAu &&
            steps == guest_insns_per_run &&
            g_runtime_cycles - cycle_start == guest_cycles_per_run;
    }
    const auto interp_end = std::chrono::steady_clock::now();
    sma2_bench_count_helpers = 0u;
    interp_ticks = sma2_bench_tick_calls;
    interp_yields = sma2_bench_yield_calls;
    const uint64_t interp_tick_ns = sma2_bench_tick_ns;
    const uint64_t interp_yield_ns = sma2_bench_yield_ns;
    const ArmCpuState interp_cpu = g_cpu;
    const uint64_t interp_iwram = hash_bytes(iwram, 32u * 1024u);
    const uint64_t interp_ewram = hash_bytes(ewram, 256u * 1024u);
    const uint8_t final_destination[2] = {
        iwram[destination_offset], iwram[destination_offset + 1u]};
    const bool no_events = g_runtime_vblank_starts == vblank_before &&
        !bus.io().irq_pending() && !bus.io().halted() &&
        headroom_now() == headroom;
    const bool pass = native_ok && interp_ok && cpu_equal(native_cpu, interp_cpu) &&
        native_entries == repetitions * 4u &&
        native_cycles == interp_cycles &&
        interp_steps == repetitions * guest_insns_per_run &&
        native_ticks == repetitions * guest_insns_per_run &&
        interp_ticks == repetitions * guest_insns_per_run &&
        native_yields == native_ticks && interp_yields == interp_ticks &&
        native_iwram == interp_iwram && native_ewram == interp_ewram &&
        native_iwram != iwram_before && native_ewram == ewram_before &&
        final_destination[0] == source_seed[0] &&
        final_destination[1] == source_seed[1] && no_events &&
        gbarecomp::self_heal_interpreted_insns() == misses_before;

    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_base);
    g_runtime_fn_entry_hook = nullptr;
    restore_memory();
    const auto native_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        native_end - native_start).count();
    const auto interp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        interp_end - interp_start).count();
    std::fprintf(stderr,
        "%s status=%s repetitions=%" PRIu32 " guest_insns=%" PRIu32
        " guest_cycles=%llu native_entries=%" PRIu32 " fallback=0"
        " native_ns=%lld interp_ns=%lld native_tick_calls=%" PRIu32
        " interp_tick_calls=%" PRIu32 " native_yield_calls=%" PRIu32
        " interp_yield_calls=%" PRIu32 " write=%02" PRIx32 "/%02" PRIx32
        " native_tick_ns=%llu interp_tick_ns=%llu"
        " native_yield_ns=%llu interp_yield_ns=%llu"
        " cpu=%s memory=%s events=%s quiet_headroom=%" PRIu32
        " helper_timing=includes_clock_probe_cost\n",
        tag, pass ? "PASS" : "MISMATCH", repetitions,
        repetitions * guest_insns_per_run,
        static_cast<unsigned long long>(native_cycles), native_entries,
        static_cast<long long>(native_ns), static_cast<long long>(interp_ns),
        native_ticks, interp_ticks, native_yields, interp_yields,
        static_cast<uint32_t>(final_destination[0]),
        static_cast<uint32_t>(final_destination[1]),
        static_cast<unsigned long long>(native_tick_ns),
        static_cast<unsigned long long>(interp_tick_ns),
        static_cast<unsigned long long>(native_yield_ns),
        static_cast<unsigned long long>(interp_yield_ns),
        cpu_equal(native_cpu, interp_cpu) ? "MATCH" : "DIFF",
        native_iwram == interp_iwram && native_ewram == interp_ewram ? "MATCH" : "DIFF",
        no_events ? "CLEAR" : "CHANGED", headroom);
    return pass;
}

bool run_timer_event_flush(gba::GbaBus& bus, gba::GbaPpu& ppu,
                           const gbarecomp::RuntimeArmContext& original) {
    constexpr const char* tag = "[sma2-cart-aot-event-flush]";
    constexpr uint16_t kTimer0If = 1u << 3;
    constexpr uint32_t kReturnPc = 0x080015FAu;
    constexpr char kRomSha1[] = "5101ddf223d1d918928fe1f306b63a42ada14a5e";
    if (!runtime_has_static_entry(kMemoryLoopEntry, 1) ||
        !runtime_has_static_entry(0x08005540u, 1) ||
        bus.read16(kMemoryLoopEntry) != kMemoryLoopOpcodes[0] ||
        bus.read16(0x08005548u) != kMemoryLoopOpcodes[8] ||
        runtime_call_stack_depth() != 0u || bus.io().irq_pending() ||
        bus.io().halted()) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=entry_guard\n", tag);
        return false;
    }

    // A disabled-DMA fixture and a timer-0 overflow are the only scheduled
    // source allowed to become due during this short generated path.
    bool dma_disabled = true;
    for (uint32_t ch = 0; ch < 4u; ++ch)
        dma_disabled = dma_disabled &&
            (bus.io().read16(0x0BAu + ch * 12u) & 0x8000u) == 0u;
    if (!dma_disabled) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=dma_active\n", tag);
        return false;
    }

    gbarecomp::runtime_restore_arm_context(original);
    g_cpu = fixture_cpu();
    // Clear previous pending sources and disable global/per-source delivery.
    bus_write_u16(0x04000200u, 0u);       // IE
    bus_write_u16(0x04000202u, 0xFFFFu);  // IF, write-one-to-clear
    bus_write_u16(0x04000208u, 0u);       // IME
    bus_write_u16(0x04000100u, 0xFFFEu); // Timer 0: overflow after 2 cycles
    bus_write_u16(0x04000102u, 0x00C0u); // enable + set IF, no timer IRQ enable in IE
    g_cpu.cpsr |= CPSR_I_BIT;

    const uint32_t ppu_headroom = ppu.cycles_until_next_event();
    const uint32_t audio_headroom = bus.audio().cycles_until_next_sample();
    const uint32_t sio_headroom = bus.io().cycles_until_next_sio_event();
    const uint32_t timer_headroom = bus.io().cycles_until_next_timer_event();
    if (timer_headroom != 2u || ppu_headroom <= 64u ||
        audio_headroom <= 64u || sio_headroom <= 64u ||
        (bus.io().read16(0x202u) & kTimer0If) != 0u) {
        gbarecomp::runtime_restore_arm_context(original);
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=event_horizon timer=%" PRIu32
            " ppu=%" PRIu32 " audio=%" PRIu32 " sio=%" PRIu32 "\n",
            tag, timer_headroom, ppu_headroom, audio_headroom, sio_headroom);
        return false;
    }

    uint64_t taken = 0u;
    uint64_t snapshot_cycles = g_runtime_cycles;
    uint64_t snapshot_vblanks = g_runtime_vblank_starts;
    gbarecomp::debug::SnapshotContext snapshot_context{};
    snapshot_context.bus = &bus;
    snapshot_context.ppu = &ppu;
    snapshot_context.rom_sha1 = kRomSha1;
    snapshot_context.taken = &taken;
    snapshot_context.cycles_elapsed = &snapshot_cycles;
    snapshot_context.vblank_count = &snapshot_vblanks;
    std::vector<uint8_t> initial_snapshot;
    std::string snapshot_error;
    if (!gbarecomp::debug::save_state_bytes(
            &initial_snapshot, snapshot_context, &snapshot_error)) {
        gbarecomp::runtime_restore_arm_context(original);
        std::fprintf(stderr, "%s status=INELIGIBLE reason=snapshot_save error=%s\n",
                     tag, snapshot_error.c_str());
        return false;
    }
    gbarecomp::RuntimeTimingContext timing_base{};
    gbarecomp::runtime_capture_timing_context(timing_base);
    auto restore_fixture = [&]() {
        gbarecomp::runtime_restore_arm_context(original);
        std::string error;
        const bool loaded = gbarecomp::debug::load_state_bytes(
            initial_snapshot.data(), initial_snapshot.size(), snapshot_context, &error);
        gbarecomp::runtime_restore_timing_context(timing_base);
        return loaded;
    };
    auto prepare_cpu = [&]() {
        g_cpu = fixture_cpu();
        g_cpu.R[15] = kMemoryLoopEntry;
        g_cpu.R[14] = kReturnPc | 1u;
        g_cpu.R[1] = kMemoryLoopSource;
        g_cpu.R[2] = 0u;
        g_cpu.R[3] = kMemoryLoopDestination;
        g_cpu.cpsr |= CPSR_I_BIT;
    };
    auto capture_snapshot = [&](std::vector<uint8_t>& out) {
        snapshot_cycles = g_runtime_cycles;
        snapshot_vblanks = g_runtime_vblank_starts;
        std::string error;
        return gbarecomp::debug::save_state_bytes(&out, snapshot_context, &error);
    };

    g_entries = 0u;
    std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
    g_runtime_fn_entry_hook = entry_hook;
    prepare_cpu();
    runtime_call_push_return(kReturnPc);
    sma2_event_trace_count = 0u;
    sma2_event_trace_origin = g_runtime_cycles;
    sma2_event_trace_active = 1u;
    const uint64_t native_cycle_start = g_runtime_cycles;
    const uint64_t native_irq_start = original.irq_entries;
    const uint64_t native_fallback_start = gbarecomp::self_heal_interpreted_insns();
    runtime_dispatch(kMemoryLoopEntry | 1u);
    sma2_event_trace_active = 0u;
    const uint32_t native_event_count = sma2_event_trace_count;
    Sma2AotEventObservation native_event{};
    if (native_event_count) native_event = sma2_event_trace[0];
    const uint64_t native_cycles = g_runtime_cycles - native_cycle_start;
    const ArmCpuState native_cpu = g_cpu;
    const uint16_t native_if = bus.io().read16(0x202u);
    const uint16_t native_timer = bus.io().read16(0x100u);
    const uint64_t native_irqs = g_runtime_irq_entries;
    const uint64_t native_fallback =
        gbarecomp::self_heal_interpreted_insns() - native_fallback_start;
    const uint32_t native_entries = g_entries;
    const uint32_t native_depth = runtime_call_stack_depth();
    gbarecomp::RuntimeTimingContext native_timing{};
    gbarecomp::runtime_capture_timing_context(native_timing);
    std::vector<uint8_t> native_snapshot;
    const bool native_snapshot_ok = capture_snapshot(native_snapshot);

    const bool restore_ok = restore_fixture();
    prepare_cpu();
    const uint64_t interp_cycle_start = g_runtime_cycles;
    const uint64_t interp_fallback_start = gbarecomp::self_heal_interpreted_insns();
    sma2_event_trace_count = 0u;
    sma2_event_trace_origin = g_runtime_cycles;
    sma2_event_trace_active = 1u;
    uint32_t interp_steps = 0u;
    while (g_cpu.R[15] != kReturnPc && interp_steps < 64u) {
        runtime_force_interp_step();
        ++interp_steps;
    }
    sma2_event_trace_active = 0u;
    const uint32_t interp_event_count = sma2_event_trace_count;
    Sma2AotEventObservation interp_event{};
    if (interp_event_count) interp_event = sma2_event_trace[0];
    const uint64_t interp_cycles = g_runtime_cycles - interp_cycle_start;
    const ArmCpuState interp_cpu = g_cpu;
    const uint16_t interp_if = bus.io().read16(0x202u);
    const uint16_t interp_timer = bus.io().read16(0x100u);
    const uint64_t interp_irqs = g_runtime_irq_entries;
    const uint64_t interp_fallback =
        gbarecomp::self_heal_interpreted_insns() - interp_fallback_start;
    const uint32_t interp_depth = runtime_call_stack_depth();
    gbarecomp::RuntimeTimingContext interp_timing{};
    gbarecomp::runtime_capture_timing_context(interp_timing);
    std::vector<uint8_t> interp_snapshot;
    const bool interp_snapshot_ok = capture_snapshot(interp_snapshot);

    const bool event_match = native_event_count == 1u && interp_event_count == 1u &&
        native_event.pc == interp_event.pc &&
        native_event.if_before == interp_event.if_before &&
        native_event.if_after == interp_event.if_after &&
        native_event.cycle_after == interp_event.cycle_after &&
        (native_event.if_after & kTimer0If) != 0u;
    const bool final_device_match = native_snapshot_ok && interp_snapshot_ok &&
        native_snapshot == interp_snapshot && native_if == interp_if &&
        native_timer == interp_timer && (native_if & kTimer0If) != 0u;
    const bool pass = restore_ok && native_entries == 2u &&
        g_entry_pcs[0] == kMemoryLoopEntry && g_entry_pcs[1] == 0x08005540u &&
        native_cpu.R[15] == kReturnPc && interp_cpu.R[15] == kReturnPc &&
        (native_cpu.cpsr & CPSR_T_BIT) && (interp_cpu.cpsr & CPSR_T_BIT) &&
        cpu_equal(native_cpu, interp_cpu) && native_cycles == interp_cycles &&
        native_cycles == 44u && interp_steps == 9u && native_fallback == 0u &&
        interp_fallback == 0u && native_irqs == native_irq_start &&
        interp_irqs == native_irq_start && native_depth == 0u && interp_depth == 0u &&
        event_match && final_device_match &&
        timing_equal(native_timing, interp_timing) &&
        !bus.io().irq_pending() && !bus.io().halted();

    if (native_snapshot_ok && interp_snapshot_ok &&
        native_snapshot != interp_snapshot)
        report_snapshot_difference(tag, native_snapshot, interp_snapshot);

    sma2_event_trace_active = 0u;
    g_runtime_fn_entry_hook = nullptr;
    gbarecomp::runtime_restore_arm_context(original);
    gbarecomp::runtime_restore_timing_context(timing_base);
    std::fprintf(stderr,
        "%s status=%s event=timer0_overflow event_pc=0x%08" PRIx32
        " event_cycle=%llu event_order=%" PRIu32
        " native_entries=%" PRIu32 " fallback_native=%llu fallback_interp=%llu"
        " exit=0x%08" PRIx32 "/0x%08" PRIx32
        " cycles=%llu/%llu steps_interp=%" PRIu32
        " cpu=%s device_snapshot=%s IF=%04x/%04x timer=%04x/%04x"
        " event_trace=%s irq_entries=%llu/%llu return_depth=%" PRIu32 "/%" PRIu32
        " restore=%s\n",
        tag, pass ? "PASS" : "MISMATCH", native_event.pc,
        static_cast<unsigned long long>(native_event.cycle_after), native_event_count,
        native_entries, static_cast<unsigned long long>(native_fallback),
        static_cast<unsigned long long>(interp_fallback), native_cpu.R[15],
        interp_cpu.R[15], static_cast<unsigned long long>(native_cycles),
        static_cast<unsigned long long>(interp_cycles), interp_steps,
        cpu_equal(native_cpu, interp_cpu) ? "MATCH" : "DIFF",
        final_device_match ? "MATCH" : "DIFF", native_if, interp_if,
        native_timer, interp_timer, event_match ? "MATCH" : "DIFF",
        static_cast<unsigned long long>(native_irqs),
        static_cast<unsigned long long>(interp_irqs), native_depth, interp_depth,
        restore_ok ? "OK" : "FAIL");
    return pass;
}
#endif

#if defined(SMA2_CART_AOT_IRQ_DIAG)
bool run_real_startup_irq(gba::GbaBus& bus, gba::GbaPpu& ppu,
                          const gbarecomp::RuntimeArmContext& cold_context,
                          const char* tag) {
    constexpr uint32_t kHandlerSlot = 0x03007FFCu;
    constexpr uint32_t kCallbackTable = 0x03007A00u;
    constexpr uint32_t kTimer0If = 1u << 3;
    constexpr uint32_t kTimer0Ie = 1u << 3;
    constexpr uint32_t kFrameCycleBudget = 2u * 280896u;
    constexpr uint32_t kStartupInstructionBudget = 1'000'000u;
    constexpr uint32_t kDispatchBudget = 1024u;
    constexpr uint32_t kDefaultTimerReload = 0xF7F8u;
    uint32_t timer_reload = kDefaultTimerReload;
    if (const char* value = std::getenv("SMA2_IRQ_TIMER_RELOAD")) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 0);
        if (end == value || *end != '\0' || parsed < 0xF5B0ul || parsed > 0xF950ul) {
            std::fprintf(stderr, "%s status=INELIGIBLE reason=timer_reload_range\n", tag);
            return false;
        }
        timer_reload = static_cast<uint32_t>(parsed);
    }

    if (gbarecomp::runtime_has_resumable_context()) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=unexpected_resumable_owner\n", tag);
        return false;
    }
    const int saved_force_interp = g_force_interp;
    const uint32_t saved_break_pc = g_runtime_break_pc;
    gbarecomp::RuntimeTimingContext cold_timing{};
    gbarecomp::runtime_capture_timing_context(cold_timing);
    uint64_t cold_taken = 0;
    uint64_t cold_cycles_elapsed = g_runtime_cycles;
    uint64_t cold_vblank_count = ppu.frame_count();
    gbarecomp::debug::SnapshotContext snapshot_context{};
    snapshot_context.bus = &bus;
    snapshot_context.ppu = &ppu;
    snapshot_context.rom_sha1 = "5101ddf223d1d918928fe1f306b63a42ada14a5e";
    snapshot_context.taken = &cold_taken;
    snapshot_context.cycles_elapsed = &cold_cycles_elapsed;
    snapshot_context.vblank_count = &cold_vblank_count;
    std::vector<uint8_t> cold_snapshot;
    std::string snapshot_error;
    if (!gbarecomp::debug::save_state_bytes(
            &cold_snapshot, snapshot_context, &snapshot_error)) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=cold_snapshot_save error=%s\n",
                     tag, snapshot_error.c_str());
        return false;
    }
    auto restore_cold = [&]() {
        gbarecomp::runtime_restore_arm_context(cold_context);
        std::string error;
        const bool restored = gbarecomp::debug::load_state_bytes(
            cold_snapshot.data(), cold_snapshot.size(), snapshot_context, &error);
        gbarecomp::runtime_restore_timing_context(cold_timing);
        g_force_interp = saved_force_interp;
        g_runtime_break_pc = saved_break_pc;
        return restored;
    };

    // Reproduce real startup in the production interpreter. Stop only at an
    // instruction boundary after the ROM-installed handler and its Timer 0
    // callback table are live; no guest pointer or callback is synthesized.
    g_force_interp = 1;
    const uint64_t startup_cycle_base = g_runtime_cycles;
    const uint64_t startup_irq_base = g_runtime_irq_entries;
    uint32_t startup_steps = 0;
    uint32_t previous_handler = bus.read32(kHandlerSlot);
    uint32_t previous_callback = bus.read32(kCallbackTable);
    uint16_t previous_ie = bus.io().read16(0x200u);
    uint16_t previous_ime = bus.io().read16(0x208u);
    constexpr uint32_t entry_pc = kMemoryLoopEntry;
    uint32_t handler = 0;
    uint32_t timer_callback = 0;
    bool ready = false;
    for (; startup_steps < kStartupInstructionBudget; ++startup_steps) {
        handler = bus.read32(kHandlerSlot);
        timer_callback = bus.read32(kCallbackTable);
        const bool handler_in_iwram = handler >= 0x03000000u && handler < 0x03007FFCu;
        const bool handler_matches_rom = handler_in_iwram &&
            bus.read32(handler) == bus.read32(0x080000FCu);
        gbarecomp::RuntimeTimingContext timing_now{};
        gbarecomp::runtime_capture_timing_context(timing_now);
        const bool bounded_event_window = timing_now.event_budget >= 8 &&
            timing_now.event_budget <= 44;
        const bool clean = g_irq_nest_depth == 0u &&
            runtime_call_stack_depth() == 0u && !bus.io().irq_pending() &&
            !bus.io().halted() && !(g_cpu.cpsr & CPSR_I_BIT) &&
            (g_cpu.cpsr & 0x1Fu) != 0x12u;
        const uint32_t callback_pc = timer_callback & ~1u;
        const bool callback_in_rom = (timer_callback & 1u) &&
            callback_pc >= 0x08000000u && callback_pc < 0x08400000u;
        if (handler_matches_rom && callback_in_rom &&
            (bus.io().read16(0x208u) & 1u) && clean && bounded_event_window &&
            !bus.io().read16(0x102u) /* Timer 0 disabled */ &&
            runtime_has_static_entry(entry_pc, 1) &&
            bus.read16(entry_pc) == kMemoryLoopOpcodes[0]) {
            ready = true;
            break;
        }
        if (g_runtime_cycles - startup_cycle_base > 4u * kFrameCycleBudget ||
            bus.io().halted()) break;
        const uint32_t executed_pc = g_cpu.R[15] & ~1u;
        runtime_force_interp_step();
        const uint32_t next_handler = bus.read32(kHandlerSlot);
        const uint32_t next_callback = bus.read32(kCallbackTable);
        const uint16_t next_ie = bus.io().read16(0x200u);
        const uint16_t next_ime = bus.io().read16(0x208u);
        if (next_handler != previous_handler) {
            std::fprintf(stderr,
                "%s startup_handler_store pc=0x%08" PRIx32
                " value=0x%08" PRIx32 "\n", tag, executed_pc, next_handler);
            previous_handler = next_handler;
        }
        if (next_callback != previous_callback) {
            std::fprintf(stderr,
                "%s startup_callback_store pc=0x%08" PRIx32
                " value=0x%08" PRIx32 "\n", tag, executed_pc, next_callback);
            previous_callback = next_callback;
        }
        if (next_ie != previous_ie || next_ime != previous_ime) {
            std::fprintf(stderr,
                "%s startup_irq_enable pc=0x%08" PRIx32
                " IE=%04x IME=%04x\n", tag, executed_pc, next_ie, next_ime);
            previous_ie = next_ie;
            previous_ime = next_ime;
        }
    }
    if (!ready || g_runtime_irq_entries != startup_irq_base) {
        const uint64_t startup_cycles = g_runtime_cycles - startup_cycle_base;
        const uint32_t stopped_pc = g_cpu.R[15];
        const bool restored = restore_cold();
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=%s startup_steps=%" PRIu32
            " startup_cycles=%llu handler=0x%08" PRIx32
            " timer0_callback=0x%08" PRIx32 " pc=0x%08" PRIx32
            " irq_delta=%llu restore=%s\n",
            tag, ready ? "startup_irq_before_checkpoint" : "real_irq_state_not_reached",
            startup_steps,
            static_cast<unsigned long long>(startup_cycles),
            handler, timer_callback, stopped_pc,
            static_cast<unsigned long long>(g_runtime_irq_entries - startup_irq_base),
            restored ? "OK" : "FAIL");
        return false;
    }
    const uint64_t startup_cycles = g_runtime_cycles - startup_cycle_base;

    gbarecomp::RuntimeTimingContext timing_ready{};
    uint64_t taken = 0;
    uint64_t cycles_elapsed = g_runtime_cycles;
    uint64_t vblank_count = ppu.frame_count();
    snapshot_context.taken = &taken;
    snapshot_context.cycles_elapsed = &cycles_elapsed;
    snapshot_context.vblank_count = &vblank_count;

    // Trigger Timer 0 through the normal GBA timer and IE/IME path. Its
    // game-installed callback is finite in this state: it polls DISPSTAT.HBLANK
    // while generated/interpreted instructions continue advancing the PPU.
    bus_write_u16(0x04000202u, kTimer0If);
    bus_write_u16(0x04000200u, bus.io().read16(0x200u) | kTimer0Ie);
    // Keep the audio timer period longer than the bounded generated loop.
    // A 2-cycle reload would re-overflow while SMA2's real timer callback is
    // still running and deliberately permits nested IRQs.
    constexpr uint32_t kTimerLoopBytes = 110u;
    std::memset(bus.ewram_ptr(), 1, kTimerLoopBytes);
    bus.ewram_ptr()[kTimerLoopBytes] = 0;
    std::memset(bus.iwram_write_ptr("sma2_corpus_dispatch_diag") + (kMemoryLoopDestination - 0x03000000u), 0xCC,
                kTimerLoopBytes + 1u);
    bus_write_u16(0x04000100u, static_cast<uint16_t>(timer_reload));
    bus_write_u16(0x04000102u, 0x00C0u);
    g_cpu = fixture_cpu();
    g_cpu.R[15] = entry_pc;
    g_cpu.R[1] = kMemoryLoopSource;
    g_cpu.R[2] = kTimerLoopBytes;
    g_cpu.R[3] = kMemoryLoopDestination;
    g_cpu.R[14] = 0x080015FAu | 1u;
    g_cpu.cpsr &= ~CPSR_I_BIT;
    runtime_call_push_return(0x080015FAu);
    gbarecomp::RuntimeArmContext test_context;
    gbarecomp::runtime_capture_arm_context(test_context);
    gbarecomp::runtime_capture_timing_context(timing_ready);
    std::vector<uint8_t> initial_snapshot;
    if (!gbarecomp::debug::save_state_bytes(
            &initial_snapshot, snapshot_context, &snapshot_error)) {
        (void)restore_cold();
        std::fprintf(stderr, "%s status=INELIGIBLE reason=snapshot_save error=%s\n",
                     tag, snapshot_error.c_str());
        return false;
    }
    auto restore_ready = [&]() {
        gbarecomp::runtime_restore_arm_context(test_context);
        std::string error;
        const bool restored = gbarecomp::debug::load_state_bytes(
            initial_snapshot.data(), initial_snapshot.size(), snapshot_context, &error);
        gbarecomp::runtime_restore_timing_context(timing_ready);
        return restored;
    };

    auto reset_trace = [&]() {
        sma2_irq_trace_count = 0;
        sma2_irq_entry_pc = 0;
        sma2_irq_vector_dispatches = 0;
        sma2_irq_vector_pc = 0;
        sma2_irq_return_arg = 0;
        sma2_irq_cpsr_at_entry = 0;
        sma2_irq_if_at_entry = 0;
        sma2_irq_ie_at_entry = 0;
        sma2_irq_ime_at_entry = 0;
        sma2_irq_return_pc = 0;
        sma2_irq_post_pc = 0;
        sma2_irq_post_target = 0;
        sma2_irq_overflow_seen = 0;
        sma2_irq_budget_expired = 0;
        sma2_irq_seen = 0;
        sma2_irq_post_step_armed = 0;
        sma2_irq_stop_pc = 0;
    };
    g_runtime_fn_entry_hook = entry_hook;
    g_runtime_break_pc = 0x080015FAu;
    g_entries = 0;
    g_entry_pc = 0;
    std::memset(g_entry_pcs, 0, sizeof(g_entry_pcs));
    reset_trace();
    g_force_interp = 0;
    sma2_irq_stop_baseline = g_runtime_irq_entries;
    sma2_irq_cycle_baseline = g_runtime_cycles;
    sma2_irq_trace_active = 1;
    sma2_irq_stop_active = 1;
    const uint64_t native_cycle_start = g_runtime_cycles;
    const uint64_t native_fallback_start = gbarecomp::self_heal_interpreted_insns();
    for (uint32_t i = 0; i < kDispatchBudget && !sma2_irq_budget_expired; ++i) {
        if (sma2_irq_seen && sma2_irq_post_pc &&
            g_cpu.R[15] == g_runtime_break_pc) break;
        if (!sma2_irq_seen && g_cpu.R[15] == 0x080015FAu) break;
        runtime_dispatch(g_cpu.R[15]);
    }
    sma2_irq_stop_active = 0;
    sma2_irq_trace_active = 0;
    const uint64_t native_cycles = g_runtime_cycles - native_cycle_start;
    const uint64_t native_fallback =
        gbarecomp::self_heal_interpreted_insns() - native_fallback_start;
    const uint64_t native_irqs = g_runtime_irq_entries - sma2_irq_stop_baseline;
    const uint32_t native_stop_pc = g_cpu.R[15];
    const uint32_t native_stop_pc_marker = g_runtime_break_pc;
    const uint32_t native_entry_count = g_entries;
    const uint32_t native_first_entry_pc = g_entries ? g_entry_pcs[0] : 0u;
    const uint32_t native_trace_count = sma2_irq_trace_count;
    const uint32_t native_vector_dispatches = sma2_irq_vector_dispatches;
    const uint32_t native_vector_pc = sma2_irq_vector_pc;
    const uint32_t native_irq_return_arg = sma2_irq_return_arg;
    const uint32_t native_irq_cpsr = sma2_irq_cpsr_at_entry;
    const uint16_t native_irq_if = sma2_irq_if_at_entry;
    const uint16_t native_irq_ie = sma2_irq_ie_at_entry;
    const uint16_t native_irq_ime = sma2_irq_ime_at_entry;
    const uint32_t native_irq_entry_pc = sma2_irq_entry_pc;
    const uint32_t native_irq_return_pc = sma2_irq_return_pc;
    const uint32_t native_post_pc = sma2_irq_post_pc;
    const bool native_budget_expired = sma2_irq_budget_expired != 0u;
    Sma2IrqTraceRow native_trace[512]{};
    std::memcpy(native_trace, sma2_irq_trace, sizeof(native_trace));
    ArmCpuState native_cpu = g_cpu;
    const uint64_t native_iwram = hash_bytes(bus.iwram_read_ptr(), 32u * 1024u);
    const uint64_t native_ewram = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    gbarecomp::RuntimeArmContext native_context;
    gbarecomp::runtime_capture_arm_context(native_context);
    std::vector<uint8_t> native_snapshot;
    runtime_mmio_catch_up();
    gbarecomp::RuntimeTimingContext native_timing{};
    gbarecomp::runtime_capture_timing_context(native_timing);
    cycles_elapsed = g_runtime_cycles;
    vblank_count = ppu.frame_count();
    const bool native_snapshot_ok = gbarecomp::debug::save_state_bytes(
        &native_snapshot, snapshot_context, &snapshot_error);

    const bool native_ran = native_snapshot_ok && native_entry_count > 0u &&
        native_first_entry_pc == entry_pc && native_irqs == 1u &&
        native_vector_dispatches == 1u && native_vector_pc == 0x18u &&
        native_post_pc != 0u && native_stop_pc_marker == native_stop_pc;

    const bool restore_ok = restore_ready();
    reset_trace();
    g_runtime_break_pc = 0x080015FAu;
    g_force_interp = 1;
    sma2_irq_stop_baseline = g_runtime_irq_entries;
    sma2_irq_cycle_baseline = g_runtime_cycles;
    sma2_irq_trace_active = 1;
    sma2_irq_stop_active = 1;
    const uint64_t interp_cycle_start = g_runtime_cycles;
    const uint64_t interp_fallback_start = gbarecomp::self_heal_interpreted_insns();
    uint32_t interp_steps = 0;
    while (interp_steps < 4096u && !sma2_irq_budget_expired) {
        if (sma2_irq_seen && sma2_irq_post_pc &&
            g_cpu.R[15] == g_runtime_break_pc) break;
        if (!sma2_irq_seen && g_cpu.R[15] == 0x080015FAu) break;
        runtime_force_interp_step();
        ++interp_steps;
    }
    sma2_irq_stop_active = 0;
    sma2_irq_trace_active = 0;
    const uint64_t interp_cycles = g_runtime_cycles - interp_cycle_start;
    const uint64_t interp_fallback =
        gbarecomp::self_heal_interpreted_insns() - interp_fallback_start;
    const uint64_t interp_irqs = g_runtime_irq_entries - sma2_irq_stop_baseline;
    const uint32_t interp_stop_pc = g_cpu.R[15];
    const uint32_t interp_trace_count = sma2_irq_trace_count;
    const uint32_t interp_vector_dispatches = sma2_irq_vector_dispatches;
    const uint32_t interp_vector_pc = sma2_irq_vector_pc;
    const uint32_t interp_irq_return_arg = sma2_irq_return_arg;
    const uint32_t interp_irq_cpsr = sma2_irq_cpsr_at_entry;
    const uint16_t interp_irq_if = sma2_irq_if_at_entry;
    const uint16_t interp_irq_ie = sma2_irq_ie_at_entry;
    const uint16_t interp_irq_ime = sma2_irq_ime_at_entry;
    const uint32_t interp_irq_return_pc = sma2_irq_return_pc;
    const uint32_t interp_post_pc = sma2_irq_post_pc;
    const bool interp_budget_expired = sma2_irq_budget_expired != 0u;
    Sma2IrqTraceRow interp_trace[512]{};
    std::memcpy(interp_trace, sma2_irq_trace, sizeof(interp_trace));
    ArmCpuState interp_cpu = g_cpu;
    const uint64_t interp_iwram = hash_bytes(bus.iwram_read_ptr(), 32u * 1024u);
    const uint64_t interp_ewram = hash_bytes(bus.ewram_ptr(), 256u * 1024u);
    gbarecomp::RuntimeArmContext interp_context;
    gbarecomp::runtime_capture_arm_context(interp_context);
    std::vector<uint8_t> interp_snapshot;
    runtime_mmio_catch_up();
    gbarecomp::RuntimeTimingContext interp_timing{};
    gbarecomp::runtime_capture_timing_context(interp_timing);
    cycles_elapsed = g_runtime_cycles;
    vblank_count = ppu.frame_count();
    const bool interp_snapshot_ok = gbarecomp::debug::save_state_bytes(
        &interp_snapshot, snapshot_context, &snapshot_error);

    bool callback_seen_native = false;
    bool callback_seen_interp = false;
    bool trace_match = native_trace_count == interp_trace_count;
    for (uint32_t i = 0; i < std::min(native_trace_count, interp_trace_count); ++i) {
        trace_match = trace_match &&
            std::memcmp(&native_trace[i], &interp_trace[i], sizeof(Sma2IrqTraceRow)) == 0;
        callback_seen_native = callback_seen_native ||
            ((native_trace[i].pc & ~1u) == (timer_callback & ~1u));
    }
    for (uint32_t i = 0; i < interp_trace_count; ++i)
        callback_seen_interp = callback_seen_interp ||
            ((interp_trace[i].pc & ~1u) == (timer_callback & ~1u));
    const bool pass = restore_ok && native_ran && interp_irqs == 1u &&
        !native_budget_expired && !interp_budget_expired &&
        native_irq_return_arg == interp_irq_return_arg &&
        native_irq_if == kTimer0If && interp_irq_if == kTimer0If &&
        native_irq_ie == kTimer0Ie && interp_irq_ie == kTimer0Ie &&
        (native_irq_ime & 1u) && (interp_irq_ime & 1u) &&
        !(native_irq_cpsr & CPSR_I_BIT) && !(interp_irq_cpsr & CPSR_I_BIT) &&
        callback_seen_native && callback_seen_interp &&
        native_stop_pc == interp_stop_pc && native_post_pc == interp_post_pc &&
        native_irq_return_pc == interp_irq_return_pc && trace_match &&
        cpu_equal(native_cpu, interp_cpu) && native_snapshot_ok && interp_snapshot_ok &&
        native_snapshot == interp_snapshot && native_cycles == interp_cycles &&
        timing_equal(native_timing, interp_timing) &&
        native_context.returns == interp_context.returns &&
        native_context.return_floor == interp_context.return_floor &&
        native_context.irq_depth == 0u && interp_context.irq_depth == 0u &&
        native_context.interrupts.empty() && interp_context.interrupts.empty() &&
        native_context.iret_depth == interp_context.iret_depth &&
        native_context.resume_pc == interp_context.resume_pc &&
        native_context.irq_from_halt == interp_context.irq_from_halt &&
        runtime_call_stack_depth() == native_context.returns.size() &&
        !bus.io().irq_pending() &&
        !bus.io().halted();

    if (native_snapshot_ok && interp_snapshot_ok && native_snapshot != interp_snapshot)
        report_snapshot_difference(tag, native_snapshot, interp_snapshot);
    if (!timing_equal(native_timing, interp_timing))
        std::fprintf(stderr,
            "%s timing_native=%llu/%llu/%llu/%llu/%lld"
            " timing_interp=%llu/%llu/%llu/%llu/%lld\n",
            tag,
            static_cast<unsigned long long>(native_timing.cycles),
            static_cast<unsigned long long>(native_timing.vblank_starts),
            static_cast<unsigned long long>(native_timing.yielded_vblank),
            static_cast<unsigned long long>(native_timing.pending_cycles),
            static_cast<long long>(native_timing.event_budget),
            static_cast<unsigned long long>(interp_timing.cycles),
            static_cast<unsigned long long>(interp_timing.vblank_starts),
            static_cast<unsigned long long>(interp_timing.yielded_vblank),
            static_cast<unsigned long long>(interp_timing.pending_cycles),
            static_cast<long long>(interp_timing.event_budget));
    std::fprintf(stderr, "%s irq_pc_trace_native=", tag);
    for (uint32_t i = 0; i < native_trace_count; ++i)
        std::fprintf(stderr, "%s%08" PRIx32, i ? "," : "", native_trace[i].pc);
    std::fprintf(stderr, " interp=");
    for (uint32_t i = 0; i < interp_trace_count; ++i)
        std::fprintf(stderr, "%s%08" PRIx32, i ? "," : "", interp_trace[i].pc);
    std::fprintf(stderr, "\n");

    g_runtime_fn_entry_hook = nullptr;
    const bool cold_restore_ok = restore_cold();
    std::fprintf(stderr,
        "%s status=%s contract=REAL_STARTUP_SYNC_IRQ entry=0x%08" PRIx32
        " handler=0x%08" PRIx32 " timer0_cb=0x%08" PRIx32
        " startup_steps=%" PRIu32 " startup_cycles=%llu"
        " native_entries=%" PRIu32 " native_fallback=%llu interp_steps=%" PRIu32
        " interp_fallback=%llu irq_entries=%llu/%llu vector_dispatch=%" PRIu32 "/%" PRIu32
        " vector_pc=0x%08" PRIx32 "/0x%08" PRIx32
        " trace_start=0x%08" PRIx32 "/0x%08" PRIx32
        " irq_arg=0x%08" PRIx32 "/0x%08" PRIx32
        " eligible_ie_if_ime=%04x/%04x/%04x:%04x/%04x/%04x"
        " cpsr_i=%u/%u callback_seen=%u handler_pcs=%" PRIu32 "/%" PRIu32
        " return_pc=0x%08" PRIx32 "/0x%08" PRIx32
        " post_irq_insn=0x%08" PRIx32 "/0x%08" PRIx32
        " stop_pc=0x%08" PRIx32 "/0x%08" PRIx32
        " cycles=%llu/%llu cpu=%s memory=%s iwram=%s ewram=%s"
        " snapshot=%s timing=%s trace=%s depth=%zu/%zu irq_pending=%u"
        " restore=%s/%s\n",
        tag, pass ? "PASS" : (restore_ok ? "MISMATCH" : "INFRASTRUCTURE_BLOCKED"),
        entry_pc, handler, timer_callback, startup_steps,
        static_cast<unsigned long long>(startup_cycles),
        native_entry_count, static_cast<unsigned long long>(native_fallback), interp_steps,
        static_cast<unsigned long long>(interp_fallback),
        static_cast<unsigned long long>(native_irqs),
        static_cast<unsigned long long>(interp_irqs), native_vector_dispatches,
        interp_vector_dispatches, native_vector_pc, interp_vector_pc,
        native_irq_entry_pc, sma2_irq_entry_pc,
        native_irq_return_arg, interp_irq_return_arg,
        native_irq_ie, native_irq_if, native_irq_ime,
        interp_irq_ie, interp_irq_if, interp_irq_ime,
        (native_irq_cpsr & CPSR_I_BIT) ? 1u : 0u,
        (interp_irq_cpsr & CPSR_I_BIT) ? 1u : 0u,
        callback_seen_native && callback_seen_interp ? 1u : 0u,
        native_trace_count, interp_trace_count,
        native_irq_return_pc, interp_irq_return_pc, native_post_pc, interp_post_pc,
        native_stop_pc, interp_stop_pc,
        static_cast<unsigned long long>(native_cycles),
        static_cast<unsigned long long>(interp_cycles),
        cpu_equal(native_cpu, interp_cpu) ? "MATCH" : "DIFF",
        native_iwram == interp_iwram && native_ewram == interp_ewram
            ? "MATCH" : "DIFF",
        native_iwram == interp_iwram ? "MATCH" : "DIFF",
        native_ewram == interp_ewram ? "MATCH" : "DIFF",
        native_snapshot_ok && interp_snapshot_ok && native_snapshot == interp_snapshot
            ? "MATCH" : "DIFF",
        timing_equal(native_timing, interp_timing) ? "MATCH" : "DIFF",
        trace_match ? "MATCH" : "DIFF", native_context.returns.size(),
        interp_context.returns.size(), bus.io().irq_pending() ? 1u : 0u,
        restore_ok ? "OK" : "FAIL",
        cold_restore_ok ? "COLD_OK" : "COLD_FAIL");
    return pass && cold_restore_ok;
}
#endif
} // namespace

extern "C" int sma2_cart_aot_diag_run() {
    using gbarecomp::active_bus;
    using gbarecomp::active_ppu;
    constexpr const char* tag = "[sma2-cart-aot-diag]";
    auto* bus = active_bus();
    auto* ppu = active_ppu();
    if (!bus || !ppu) {
        std::fprintf(stderr, "%s status=INELIGIBLE reason=no_active_bus_or_ppu\n", tag);
        return 0;
    }

    const uint32_t return_depth = runtime_call_stack_depth();
    uint32_t event_headroom = std::min({
        ppu->cycles_until_next_event(),
        bus->audio().cycles_until_next_sample(),
        bus->io().cycles_until_next_timer_event(),
        bus->io().cycles_until_next_sio_event()});
    if (!runtime_has_static_entry(kEntry, 1) ||
        bus->read16(kEntry) != kThumbBxLr || return_depth != 0 ||
        bus->io().irq_pending() || bus->io().halted()) {
        std::fprintf(stderr,
            "%s status=INELIGIBLE static=%d opcode=%04" PRIx32 " depth=%" PRIu32 " irq=%d halted=%d event_headroom=%" PRIu32 "\n",
            tag, runtime_has_static_entry(kEntry, 1), static_cast<uint32_t>(bus->read16(kEntry)),
            return_depth, bus->io().irq_pending() ? 1 : 0,
            bus->io().halted() ? 1 : 0, event_headroom);
        return 0;
    }

    gbarecomp::RuntimeArmContext original;
    gbarecomp::runtime_capture_arm_context(original);
#if !defined(SMA2_CART_AOT_DIAG_ISOLATED_ONLY)
    bool irq_pass = true;
#if defined(SMA2_CART_AOT_IRQ_DIAG)
    // This must run against the boot state before other fixtures modify timer
    // or event state. The fixture restores the full cold runtime snapshot.
    irq_pass = run_real_startup_irq(*bus, *ppu, original, "[sma2-cart-aot-irq]");
#endif
    const bool arm_entry_pass = run_arm_reset_entry(
        *bus, original, "[sma2-cart-aot-arm]");
    set_fixture_cpu();  // IRQ/FIQ remain masked while aligning to a quiet event window.
    for (unsigned i = 0; event_headroom <= 64u && i < 4u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, event_headroom));
        event_headroom = std::min({
            ppu->cycles_until_next_event(),
            bus->audio().cycles_until_next_sample(),
            bus->io().cycles_until_next_timer_event(),
            bus->io().cycles_until_next_sio_event()});
    }
    if (event_headroom <= 64u || bus->io().irq_pending()) {
        gbarecomp::runtime_restore_arm_context(original);
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=no_quiet_event_window event_headroom=%" PRIu32 " irq=%d\n",
            tag, event_headroom, bus->io().irq_pending() ? 1 : 0);
        return 0;
    }
#else
    // The first-step hook may land immediately before an audio event. Preserve
    // the original isolated fixture's quiet-window setup without running the
    // heavyweight IRQ differential or serializing the full machine state.
    set_fixture_cpu();
    for (unsigned i = 0; event_headroom <= 64u && i < 4u; ++i) {
        runtime_tick(std::max<uint32_t>(1u, event_headroom));
        event_headroom = std::min({
            ppu->cycles_until_next_event(),
            bus->audio().cycles_until_next_sample(),
            bus->io().cycles_until_next_timer_event(),
            bus->io().cycles_until_next_sio_event()});
    }
    if (event_headroom <= 64u || bus->io().irq_pending()) {
        gbarecomp::runtime_restore_arm_context(original);
        std::fprintf(stderr,
            "%s status=INELIGIBLE reason=no_quiet_event_window event_headroom=%" PRIu32 " irq=%d\n",
            tag, event_headroom, bus->io().irq_pending() ? 1 : 0);
        return 0;
    }
#endif
    const uint32_t old_break_pc = g_runtime_break_pc;
    const uint64_t iwram_before = hash_bytes(bus->iwram_read_ptr(), 32u * 1024u);
    const uint64_t ewram_before = hash_bytes(bus->ewram_ptr(), 256u * 1024u);
    const uint64_t vblank_before = g_runtime_vblank_starts;
    g_runtime_break_pc = 0;

    set_fixture_cpu();
    g_entries = 0;
    g_entry_pc = 0;
    g_runtime_fn_entry_hook = entry_hook;
    runtime_call_push_return(kReturn);
    const uint64_t native_start = g_runtime_cycles;
    runtime_dispatch(kEntry | 1u);
    const RunResult native = capture(native_start, *bus);
    const bool native_entry = g_entries == 1 && g_entry_pc == kEntry;

    gbarecomp::runtime_restore_arm_context(original);
    set_fixture_cpu();
    const uint64_t interp_start = g_runtime_cycles;
    runtime_force_interp_step();
    const RunResult interp = capture(interp_start, *bus);

    const bool exit_match = native.cpu.R[15] == kReturn &&
        interp.cpu.R[15] == kReturn &&
        (native.cpu.cpsr & CPSR_T_BIT) && (interp.cpu.cpsr & CPSR_T_BIT);
    const bool memory_match = native.iwram == iwram_before &&
        interp.iwram == iwram_before && native.ewram == ewram_before &&
        interp.ewram == ewram_before;
    const bool events_clear = native.vblank == vblank_before &&
        interp.vblank == vblank_before &&
        native.irq_entries == original.irq_entries &&
        interp.irq_entries == original.irq_entries;
    const bool pass = native_entry && cpu_equal(native.cpu, interp.cpu) &&
        exit_match && memory_match && events_clear &&
        native.cycles == interp.cycles && native.return_depth == 0 &&
        interp.return_depth == 0;

    gbarecomp::runtime_restore_arm_context(original);
    g_runtime_break_pc = old_break_pc;
    std::fprintf(stderr,
        "%s status=%s contract=ISOLATED entry=0x%08" PRIx32 " mode=thumb opcode=%04" PRIx32 " "
        "native_entries=%" PRIu32 " selected_pc=0x%08" PRIx32 " exit_native=0x%08" PRIx32 " "
        "exit_interp=0x%08" PRIx32 " cycles_native=%llu cycles_interp=%llu "
        "cpu=%s iwram=%s ewram=%s events=%s return_depth=%" PRIu32 "/%" PRIu32 "\n",
        tag, pass ? "PASS" : "MISMATCH", kEntry, kThumbBxLr, g_entries,
        g_entry_pc, native.cpu.R[15], interp.cpu.R[15],
        static_cast<unsigned long long>(native.cycles),
        static_cast<unsigned long long>(interp.cycles),
        cpu_equal(native.cpu, interp.cpu) ? "MATCH" : "DIFF",
        native.iwram == iwram_before && interp.iwram == iwram_before ? "MATCH" : "DIFF",
        native.ewram == ewram_before && interp.ewram == ewram_before ? "MATCH" : "DIFF",
        events_clear ? "CLEAR" : "CHANGED", native.return_depth,
        interp.return_depth);
#if defined(SMA2_CART_AOT_DIAG_ISOLATED_ONLY)
    return pass ? 1 : 0;
#else
    const bool chain_pass = run_two_function_chain(*bus, *ppu, original, tag);
    const bool memory_no_branch = run_memory_branch_case(*bus, *ppu, original, 0u);
    const bool memory_taken_branch = run_memory_branch_case(*bus, *ppu, original, 1u);
    const bool fallback_pass = run_fallback_reentry(*bus, *ppu, original, tag);
    // swi #0xc CpuFastSet copy (16 words) / fill; swi #0xb CpuSet halfword copy (32 halfwords).
    const bool swi_fast_copy = run_swi_case(*bus, *ppu, original,
        {"cpufastset_copy16w", 0x0809DFBCu, 0xDF0Cu, 16u, 64u, false});
    const bool swi_fast_fill = run_swi_case(*bus, *ppu, original,
        {"cpufastset_fill16w", 0x0809DFBCu, 0xDF0Cu, 16u | (1u << 24), 64u, true});
    const bool swi_cpuset_copy = run_swi_case(*bus, *ppu, original,
        {"cpuset_copy32h", 0x0809DFC0u, 0xDF0Bu, 32u, 64u, false});
#if defined(SMA2_CART_AOT_BENCH)
    const bool bench_pass = run_chain_benchmark(*bus, *ppu, original);
    const bool memory_bench_pass = run_memory_branch_benchmark(*bus, *ppu, original);
    const bool event_flush_pass = run_timer_event_flush(*bus, *ppu, original);
#else
    const bool bench_pass = true;
    const bool memory_bench_pass = true;
    const bool event_flush_pass = true;  // run_timer_event_flush is BENCH-only
#endif
    return pass && chain_pass && memory_no_branch && memory_taken_branch &&
        fallback_pass && swi_fast_copy && swi_fast_fill && swi_cpuset_copy &&
        bench_pass && memory_bench_pass && event_flush_pass && irq_pass &&
        arm_entry_pass ? 1 : 0;
#endif
}
