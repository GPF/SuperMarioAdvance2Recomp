#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "arm_cpu_bridge.h"
#include "gba_bus.h"
#include "gba_waitstates.h"
#include "interpreter.h"
#include "runtime.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "thumb_decode.h"

extern "C" void sma2_aot_stage_b(void);
extern "C" unsigned long long g_runtime_cycles;

namespace {
constexpr uint32_t kEntry = 0x080015ECu;
constexpr uint32_t kExit = 0x0809DFBCu;
constexpr unsigned kMaxSteps = 512;
constexpr unsigned kMaxInstructions = 64;
constexpr unsigned kMaxWrites = 16;

struct Write {
    uint32_t address = 0;
    uint32_t value = 0;
    uint8_t width = 0;
};

struct Replay {
    armv4t::CPUState cpu{};
    std::array<uint8_t, 0x8000> iwram{};
    RuntimeWaitTable waits{};
    std::array<Write, kMaxWrites> writes{};
    unsigned write_count = 0;
    unsigned instructions = 0;
    uint32_t cycles = 0;
    bool failed = false;
};

class ShadowBus final : public armv4t::Bus {
public:
    ShadowBus(Replay& replay, gba::GbaBus& live) : replay_(replay), live_(live) {}
    uint8_t read8(uint32_t a) override {
        if (a >= 0x03000000u && a < 0x04000000u) return replay_.iwram[a & 0x7FFFu];
        if (a >= 0x08000000u && a < 0x0E000000u) return live_.read8(a);
        replay_.failed = true; return 0;
    }
    uint16_t read16(uint32_t a) override {
        return uint16_t(read8(a)) | (uint16_t(read8(a + 1u)) << 8);
    }
    uint32_t read32(uint32_t a) override {
        return uint32_t(read16(a)) | (uint32_t(read16(a + 2u)) << 16);
    }
    void write8(uint32_t a, uint8_t v) override { write(a, v, 1); }
    void write16(uint32_t a, uint16_t v) override { write(a, v, 2); }
    void write32(uint32_t a, uint32_t v) override { write(a, v, 4); }
    uint32_t access_cycles(uint32_t a, uint8_t w, bool s) const override {
        return gba::waits_access_cycles(replay_.waits, a, w, s);
    }
    uint32_t code_wait(uint32_t pc, bool thumb, bool s) const override {
        return gba::waits_code(replay_.waits, pc, thumb, s);
    }
    int32_t prefetch_stall(int32_t wait, uint32_t pc, bool thumb) override {
        return gba::waits_prefetch_stall(replay_.waits, wait, pc, thumb);
    }
private:
    void write(uint32_t a, uint32_t v, uint8_t width) {
        if (a < 0x03000000u || a >= 0x04000000u ||
            replay_.write_count >= replay_.writes.size()) {
            replay_.failed = true; return;
        }
        const uint32_t off = a & 0x7FFFu;
        for (uint8_t i = 0; i < width; ++i)
            replay_.iwram[(off + i) & 0x7FFFu] = uint8_t(v >> (i * 8u));
        replay_.writes[replay_.write_count++] = {a, v, width};
    }
    Replay& replay_;
    gba::GbaBus& live_;
};

bool equal_cpu(const ArmCpuState& a, const ArmCpuState& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

bool done = false;
bool aot_dispatched = false;
bool aot_dispatch_returned = false;
bool continuation_seen = false;
bool block_comparison_passed = false;
unsigned outer_steps = 0;
uint64_t continuation_cycles_before = 0;
uint64_t continuation_cycle_delta = 0;
const char* final_status = "not-started";
const char* final_detail = "none";

bool run_diagnostic() {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) {
        final_status = "INFRASTRUCTURE_BLOCKED";
        final_detail = "no_active_bus";
        return false;
    }
    Replay replay;
    replay.iwram = {};
    std::memcpy(replay.iwram.data(), bus->iwram_read_ptr(), replay.iwram.size());
    replay.waits = g_runtime_waits;
    gbarecomp::load_arm_cpu_into_interp(g_cpu, replay.cpu);
    ShadowBus shadow(replay, *bus);

    while (!replay.failed && replay.instructions < kMaxInstructions &&
           replay.cpu.R[15] != kExit) {
        if (replay.cpu.R[15] < 0x08000000u || replay.cpu.R[15] >= 0x0E000000u ||
            !replay.cpu.cpsr.t) { replay.failed = true; break; }
        const uint32_t pc = replay.cpu.R[15];
        const auto ins = armv4t::ThumbDecoder::decode(shadow.read16(pc), pc);
        uint32_t cycles = 0;
        const auto result = armv4t::Interpreter::step(replay.cpu, shadow, ins, &cycles);
        ++replay.instructions;
        replay.cycles += cycles;
        if (result == armv4t::Interpreter::Result::NotImplemented ||
            result == armv4t::Interpreter::Result::Undefined ||
            result == armv4t::Interpreter::Result::Swi) replay.failed = true;
    }
    if (replay.failed || replay.cpu.R[15] != kExit ||
        replay.instructions > kMaxInstructions || replay.write_count > kMaxWrites) {
        final_status = "INELIGIBLE";
        final_detail = "unsupported_or_over_budget_interpreter_replay";
        return false;
    }

    const uint32_t store_address = g_cpu.R[13] + 4u;
    if (store_address < 0x03000000u || store_address + 4u > 0x04000000u) {
        final_status = "INELIGIBLE";
        final_detail = "store_outside_iwram";
        return false;
    }
    std::array<uint8_t, 0x8000> before_iwram{};
    std::memcpy(before_iwram.data(), bus->iwram_read_ptr(), before_iwram.size());
    const ArmCpuState before_cpu = g_cpu;
    const uint64_t cycles_before = g_runtime_cycles;
    sma2_aot_stage_b();
    aot_dispatched = true;
    const uint64_t native_cycles = g_runtime_cycles - cycles_before;
    ArmCpuState expected{};
    gbarecomp::store_interp_into_arm_cpu(replay.cpu, expected);

    unsigned cpu_diffs = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (g_cpu.R[i] != expected.R[i]) {
            ++cpu_diffs;
            std::fprintf(stderr, "[sma2-aot] R%u native=%08X interp=%08X\n",
                         i, unsigned(g_cpu.R[i]), unsigned(expected.R[i]));
        }
    }
    if (g_cpu.cpsr != expected.cpsr) {
        ++cpu_diffs;
        std::fprintf(stderr, "[sma2-aot] CPSR native=%08X interp=%08X\n",
                     unsigned(g_cpu.cpsr), unsigned(expected.cpsr));
    }
    auto compare_bank = [&](const char* name, const uint32_t* native,
                            const uint32_t* interp, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            if (native[i] == interp[i]) continue;
            ++cpu_diffs;
            std::fprintf(stderr,
                         "[sma2-aot] %s[%u] native=%08X interp=%08X\n",
                         name, unsigned(i), unsigned(native[i]), unsigned(interp[i]));
        }
    };
    compare_bank("banked_sp", g_cpu.banked_sp, expected.banked_sp,
                 std::size(g_cpu.banked_sp));
    compare_bank("banked_lr", g_cpu.banked_lr, expected.banked_lr,
                 std::size(g_cpu.banked_lr));
    compare_bank("banked_spsr", g_cpu.banked_spsr, expected.banked_spsr,
                 std::size(g_cpu.banked_spsr));
    compare_bank("r8_12_user", g_cpu.r8_12_user, expected.r8_12_user,
                 std::size(g_cpu.r8_12_user));
    compare_bank("r8_12_fiq", g_cpu.r8_12_fiq, expected.r8_12_fiq,
                 std::size(g_cpu.r8_12_fiq));

    unsigned iwram_diffs = 0;
    unsigned native_changed_bytes = 0;
    const uint8_t* live_iwram = bus->iwram_read_ptr();
    for (std::size_t i = 0; i < replay.iwram.size(); ++i) {
        if (live_iwram[i] != before_iwram[i]) ++native_changed_bytes;
        if (live_iwram[i] != replay.iwram[i]) {
            std::fprintf(stderr, "[sma2-aot] IWRAM[%04X] native=%02X interp=%02X\n",
                         unsigned(i), live_iwram[i], replay.iwram[i]);
            ++iwram_diffs;
        }
    }
    const bool state_match = cpu_diffs == 0 && equal_cpu(g_cpu, expected);
    const bool memory_match = iwram_diffs == 0;
    const bool cycles_match = native_cycles == replay.cycles;
    const bool branch_match = g_cpu.R[15] == kExit &&
                              (g_cpu.cpsr & CPSR_T_BIT) != 0;
    const bool unchanged_banks = std::memcmp(before_cpu.banked_sp, g_cpu.banked_sp,
        sizeof(g_cpu.banked_sp)) == 0 && std::memcmp(before_cpu.banked_lr, g_cpu.banked_lr,
        sizeof(g_cpu.banked_lr)) == 0 && std::memcmp(before_cpu.banked_spsr, g_cpu.banked_spsr,
        sizeof(g_cpu.banked_spsr)) == 0;
    const bool pass = state_match && memory_match && cycles_match && branch_match && unchanged_banks;
    block_comparison_passed = pass;
    final_status = pass ? "BLOCK_MATCH" : "BLOCK_MISMATCH";
    std::fprintf(stderr,
        "[sma2-aot-dispatch] block=%s entry=%08X exit=%08X mode=thumb instructions=%u "
        "interp_cycles=%u native_cycles=%llu writes=%u cpu_diffs=%u "
        "native_changed_bytes=%u iwram_diff_bytes=%u branch=%s banked=%s selected=normal-dispatch\n",
        final_status, unsigned(kEntry), unsigned(g_cpu.R[15]),
        replay.instructions, unsigned(replay.cycles),
        static_cast<unsigned long long>(native_cycles), replay.write_count,
        cpu_diffs, native_changed_bytes, iwram_diffs,
        branch_match ? "match" : "mismatch",
        unchanged_banks ? "match" : "mismatch");
    return true;
}
}  // namespace

extern "C" uint32_t sma2_aot_diagnostic_step(void) {
    if (done) return 1u;
    if (++outer_steps > kMaxSteps) {
        final_status = aot_dispatched ? "BUDGET_EXHAUSTED" : "NOT_REACHED";
        std::fprintf(stderr, "[sma2-aot-dispatch] status=%s entry=%08X steps=%u aot_dispatches=%u fallback=%u\n",
                     final_status, unsigned(kEntry), outer_steps - 1u,
                     aot_dispatched ? 1u : 0u, aot_dispatched ? 1u : 0u);
        done = true;
        return 1u;
    }
    if (aot_dispatched && g_cpu.R[15] == kExit &&
        (g_cpu.cpsr & CPSR_T_BIT)) {
        continuation_seen = true;
        continuation_cycles_before = g_runtime_cycles;
        std::fprintf(stderr,
            "[sma2-aot-dispatch] boundary=2 entry=%08X mode=thumb path=interpreter-bridge "
            "classification=SWI_0C exit_target=%08X cpu_comparison=not-run "
            "iwram_comparison=not-run fallback=1\n",
            unsigned(kExit), unsigned(kExit));
    }
    return 0u;
}

// Called by GBARecomp's ordinary dispatch resolver only after the immutable
// generated table and existing native overlay both decline this exact PC.
extern "C" int sma2_aot_dispatch_try(uint32_t pc, int thumb) {
    if (done || aot_dispatched || pc != kEntry || thumb != 1 ||
        (g_cpu.cpsr & CPSR_T_BIT) == 0) return 0;
    if (!run_diagnostic()) {
        done = true;
        std::fprintf(stderr,
            "[sma2-aot-dispatch] status=%s reason=%s entry=%08X mode=thumb "
            "path=existing-interpreter-fallback aot_dispatches=0 fallback=1\n",
            final_status, final_detail, unsigned(pc));
        return 0;
    }
    std::fprintf(stderr,
        "[sma2-aot-dispatch] boundary=1 entry=%08X mode=thumb path=linked-aot "
        "symbol=sma2_aot_stage_b static_owner=miss overlay_owner=miss fallback=0\n",
        unsigned(pc));
    return 1;
}

// The first return is the Stage B AOT block. Let the next normal runner
// dispatch process its SWI thunk through GBARecomp's established bridge, then
// stop once that bounded call returns to the BL continuation.
extern "C" uint32_t sma2_aot_diagnostic_after_dispatch(void) {
    if (done && !aot_dispatched) return 1u;
    if (!aot_dispatched) return 0u;
    if (!aot_dispatch_returned) {
        aot_dispatch_returned = true;
        return 0u;
    }
    if (!continuation_seen) return 0u;
    const bool returned = g_cpu.R[15] == 0x080015FAu &&
                          (g_cpu.cpsr & CPSR_T_BIT) != 0;
    continuation_cycle_delta = g_runtime_cycles - continuation_cycles_before;
    const bool pass = block_comparison_passed && returned;
    final_status = pass ? "PASS" : (block_comparison_passed ? "MISMATCH" : "MISMATCH");
    std::fprintf(stderr,
        "[sma2-aot-dispatch] status=%s boundaries=2 aot_dispatches=1 "
        "continuation_pc=%08X expected=080015FA mode=%s continuation_cycles=%llu fallback=1 "
        "transition=SWI_0C_then_BX_LR\n",
        final_status, unsigned(g_cpu.R[15]),
        (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm",
        static_cast<unsigned long long>(continuation_cycle_delta));
    done = true;
    return 1u;
}
