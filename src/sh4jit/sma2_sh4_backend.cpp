// sma2_sh4_backend.cpp — SMA2-local ARM/Thumb -> SH-4 dynamic-recompilation
// CPU backend, built on the vendored Bruce mGBA SH-4 dynarec
// (vendor/mgba-sh4-jit, revision recorded in vendor/mgba-sh4-jit/PROVENANCE.md).
//
// Boundary (see docs/results/SMA2_SH4_DYNAREC_INTEGRATION.md):
//
//   SMA2 frontend -> GBARecomp runtime -> { bus/MMIO, PPU, audio, DMA/timers/IRQ,
//   scheduler } -> CPU backend -> { SH-4 dynarec | production interpreter }
//
// The dynarec owns ONLY guest instruction execution and guest data/instruction
// fetch. Every device side effect, every MMIO register, every cycle the master
// clock advances, and every IRQ/DMA/PPU event still goes through GBARecomp:
//
//   * guest loads/stores      -> bus_read_u32 / bus_write_u16 / ... (runtime_bus_bridge)
//   * guest access costs      -> runtime_mem_cycles / runtime_prefetch_stall_delta
//   * master clock + devices  -> runtime_tick
//   * next scheduled event    -> runtime_event_budget (overlay accessor)
//   * SWI / BIOS entry        -> runtime_swi
//   * scheduler yields        -> runtime_should_yield
//
// Facts below are from the vendored sources, not from upstream mGBA in general.

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "runtime.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "arm_cpu_bridge.h"
#include "interpreter.h"
#include "thumb_decode.h"
#include "gba_waitstates.h"

#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/jit.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>
#include <mgba/internal/gba/io.h>

// ── GBARecomp services the backend calls ─────────────────────────────
extern "C" uint32_t bus_read_u32(uint32_t addr);
extern "C" uint16_t bus_read_u16(uint32_t addr);
extern "C" uint8_t bus_read_u8(uint32_t addr);
extern "C" void bus_write_u32(uint32_t addr, uint32_t val);
extern "C" void bus_write_u16(uint32_t addr, uint16_t val);
extern "C" void bus_write_u8(uint32_t addr, uint8_t val);
extern "C" uint32_t runtime_mem_cycles(uint32_t addr, uint32_t width,
                                       uint32_t sequential);
extern "C" uint32_t runtime_prefetch_stall_delta(uint32_t wait, uint32_t pc,
                                                 uint32_t thumb);
extern "C" void runtime_tick(uint32_t cycles);
extern "C" bool runtime_should_yield(void);
extern "C" void runtime_swi(uint32_t swi_imm);
extern "C" void runtime_resync_horizon(void);
extern "C" long long runtime_event_budget(void);

extern ArmCpuState g_cpu;
extern unsigned long long g_runtime_cycles;

// ── mGBA's own globals that the dynarec references ───────────────────
// jit-emit.c's _emitVramStore tests JIT_MD_VRAM, which ARMJITVramFast()
// returns 0 for while GBAVideoSoftwareLineHook is null, so the VRAM-store
// fast path is inert and VRAM stores reach the C slow path. These must have
// C linkage: jit.c/ARMJITUpdateMemory reference the plain C names.
extern "C" {
void (*GBAVideoSoftwareLineHook)(struct GBAVideoSoftwareRenderer*, int) = nullptr;
struct GBAVideoSoftwareRenderer* GBAVideoSoftwareLineHookTarget = nullptr;
uint8_t GBAVideoSoftwareVRAMUnits[SIZE_VRAM / 32];
}

namespace sma2 {

// ── The backend's machine ────────────────────────────────────────────
// mGBA-layout data only. No mGBA frontend, video, audio or platform code is
// linked; the fields the dynarec and the ARM core actually read are filled in
// by init_backend().
static struct GBA gba;
static struct ARMCore cpu;
static bool backend_ready = false;
static bool backend_init_failed = false;
static bool rom_mask_supported = false;
static bool jit_illegal_seen = false;
static uint32_t jit_illegal_pc = 0;
static uint32_t jit_illegal_opcode = 0;

// Bounded stage-by-stage differential. The shadow bus owns a private IWRAM image
// and wait table, so replaying the production interpreter cannot mutate live
// memory, prefetch state, devices, or scheduler state.
//
//   Stage A: the interval already validated on physical hardware,
//            Thumb 0x080015D8 -> 0x0809DFBC. Its comparison semantics are
//            unchanged; it must pass before stage B is armed.
//   Stage B: one additional block, beginning at the guest PC the live
//            SWI/runtime trampoline returns to (Thumb 0x080015EC). The
//            trampoline itself is executed normally and is never intercepted,
//            simulated or modified.
//   Stage C: one block at the source-derived continuation 0x080015FA, armed
//            only after A and B pass and only from committed live CPU state.
//   Stage D: the next caller block at 0x0800160A, armed only after the
//            intervening live SWI/BX LR path commits there. The SWI is outside
//            the replay and its CPUSet/VRAM side effects are not validated.
//   Stage E: the next caller block at 0x0800161A, armed only after Stage D
//            passes and the live CPUFastSet SWI/BX LR path commits there.
static constexpr uint32_t kStageAEntryPc = 0x080015D8u;
static constexpr uint32_t kStageAExitPc = 0x0809DFBCu;
static constexpr uint32_t kStageBEntryPc = 0x080015ECu;
static constexpr uint32_t kStageCEntryPc = 0x080015FAu;
static constexpr uint32_t kStageCStrhPc = 0x080015FEu;
static constexpr uint32_t kStageDEntryPc = 0x0800160Au;
static constexpr uint32_t kStageDStrhPc = 0x08001610u;
static constexpr uint32_t kStageEEntryPc = 0x0800161Au;
static constexpr unsigned kDiffMaxInstructions = 64;
static constexpr unsigned kDiffMaxWrites = 16;
// Finite, explicit traversal budget between the stage-A comparison and the
// stage-B entry. The earlier hardware trace observed this return after five
// bounded block runs; that is a reference observation, not a guarantee, so this
// is headroom. Exhausting the budget reports INELIGIBLE instead of looking for
// another eligible block.
static constexpr unsigned kStageBMaxBlockRuns = 32;
static constexpr unsigned kStageBMaxSteps = 128;
static constexpr unsigned kStageCMaxBlockRuns = 32;
static constexpr unsigned kStageCMaxSteps = 128;
// Stage D is the direct return-caller block after one known SWI thunk and BX LR.
// Eight native blocks / 32 outer steps allow the expected SWI handoff and
// return while stopping before a broader gameplay walk.
static constexpr unsigned kStageDMaxBlockRuns = 8;
static constexpr unsigned kStageDMaxSteps = 32;
// Same narrow headroom for the one known post-D SWI return path.
static constexpr unsigned kStageEMaxBlockRuns = 8;
static constexpr unsigned kStageEMaxSteps = 32;
// After the Stage E pass, observe only the immediate SWI return window and at
// most four consecutive eligible native blocks. These are fixed safety caps,
// not a gameplay traversal target.
static constexpr unsigned kFirstDivMaxBlocks = 4;
static constexpr unsigned kFirstDivMaxOuterSteps = 16;
static constexpr unsigned kFirstDivMaxTransitionRuns = 8;
static constexpr unsigned kMaxDiagnosticSteps = 512;

static const char* const kExitReasonNames[] = {
    "event", "site_fall", "site_branch", "branch", "sync", "lookup"
};
struct DiffWrite {
    uint32_t address;
    uint32_t value;
    uint8_t width;
};
struct IntervalDiff {
    bool active = false;      // live path is being traced against this snapshot
    bool ready = false;       // replay reached an equivalent boundary on its own
    bool failed = false;      // SWI, unsupported access, undefined, or a limit hit
    const char* status = nullptr;   // PASS / MISMATCH / INELIGIBLE / BLOCKED
    uint32_t entry_pc = 0;
    uint32_t exit_pc = 0;     // boundary the replay must reach by its own semantics
    uint32_t instructions = 0;
    uint32_t cycles = 0;      // replay cycles charged from the snapshot
    int32_t native_start_cycles = 0;
    uint32_t native_cycles = 0;
    uint32_t write_count = 0;
    DiffWrite writes[kDiffMaxWrites] = {};
    uint32_t native_write_count = 0;
    DiffWrite native_writes[kDiffMaxWrites] = {};
    armv4t::CPUState snapshot = {};   // entry snapshot: replay start state
    armv4t::CPUState expected = {};   // replay end state (compared against native)
    uint8_t iwram[0x8000] = {};       // private copy: replay scratch + native baseline
    RuntimeWaitTable waits = {};
    // Native-block facts for the reusable one-block stage comparisons.
    const char* native_exit_reason = "none";
    char native_exit_detail[64] = {};
    unsigned native_exit_reason_count = 0;
    bool native_exit_thumb = false;
    uint32_t native_block_runs = 0;
    uint32_t native_fallback_steps = 0;
    // How the replay established the boundary: the decoded instruction whose own
    // semantics moved PC there, recorded so a coincidental arrival is visible.
    uint32_t boundary_pc = 0;
    uint16_t boundary_hw = 0;
    const char* boundary_via = "none";
    // Additive reporting only; the pass/fail decision logic is unchanged.
    unsigned cpu_mismatches = 0;
    unsigned iwram_mismatch_bytes = 0;
    bool state_match = false, cpsr_match = false, memory_match = false;
    bool cycles_match = false, writes_consistent = false;
    uint64_t entry_guest_cycles = 0;
    uint32_t strh_effective_address = 0, strh_value = 0;
    uint8_t strh_access_width = 0;
    const char* strh_region = "not-captured";
};
struct FirstDivBlockRecord {
    const char* status = "not-run";
    uint32_t entry_pc = 0, exit_pc = 0;
    bool entry_thumb = false, exit_thumb = false;
    const char* exit_reason = "none";
    uint32_t block_runs = 0, fallback_steps = 0, instructions = 0;
    uint32_t native_cycles = 0, interp_cycles = 0, mismatches = 0;
};
static IntervalDiff stage_a, stage_b, stage_c, stage_d, stage_e;
static IntervalDiff first_div_diff;
static FirstDivBlockRecord first_div_blocks[kFirstDivMaxBlocks] = {};
static unsigned first_div_attempts, first_div_passes;
static unsigned first_div_outer_steps, first_div_transition_runs;
static long long first_div_event_budget;
static bool first_div_waiting_post_e_swi, first_div_active;
static bool first_div_single_block;
static const char* first_div_status = "not-started";
static const char* first_div_stop_reason = "not-started";
static bool stage_a_completed;
static bool stage_b_completed;
static bool stage_c_completed;
static bool stage_d_completed;
static bool stage_b_single_block;   // the next ARMJITRun is the isolated block
static bool stage_c_single_block;
static bool stage_d_single_block;
static bool stage_e_single_block;
static bool stage_c_armed;
static bool stage_d_armed;
static bool stage_e_armed;
static bool diagnostic_finished;    // gate that stops the outer game runner
static unsigned stage_b_steps, stage_b_block_runs;
static unsigned stage_c_steps, stage_c_block_runs, diagnostic_steps;
static unsigned stage_d_steps, stage_d_block_runs;
static unsigned stage_e_steps, stage_e_block_runs;

class IntervalShadowBus final : public armv4t::Bus {
public:
    explicit IntervalShadowBus(IntervalDiff& d, gba::GbaBus& live)
        : diff(d), bus(live) {}
    uint8_t read8(uint32_t a) override {
        if (a >= 0x03000000u && a < 0x04000000u)
            return diff.iwram[a & 0x7FFFu];
        if (a >= 0x08000000u && a < 0x0E000000u) return bus.read8(a);
        diff.failed = true; return 0;
    }
    uint16_t read16(uint32_t a) override {
        if (a >= 0x03000000u && a < 0x04000000u)
            return (uint16_t)read8(a) | ((uint16_t)read8(a + 1) << 8);
        if (a >= 0x08000000u && a < 0x0E000000u) return bus.read16(a);
        diff.failed = true; return 0;
    }
    uint32_t read32(uint32_t a) override {
        if (a >= 0x03000000u && a < 0x04000000u)
            return (uint32_t)read16(a) | ((uint32_t)read16(a + 2) << 16);
        if (a >= 0x08000000u && a < 0x0E000000u) return bus.read32(a);
        diff.failed = true; return 0;
    }
    void write8(uint32_t a, uint8_t v) override { write(a, v, 1); }
    void write16(uint32_t a, uint16_t v) override { write(a, v, 2); }
    void write32(uint32_t a, uint32_t v) override { write(a, v, 4); }
    uint32_t access_cycles(uint32_t a, uint8_t w, bool s) const override {
        return gba::waits_access_cycles(diff.waits, a, w, s);
    }
    uint32_t code_wait(uint32_t pc, bool thumb, bool s) const override {
        return gba::waits_code(diff.waits, pc, thumb, s);
    }
    int32_t prefetch_stall(int32_t wait, uint32_t pc, bool thumb) override {
        return gba::waits_prefetch_stall(diff.waits, wait, pc, thumb);
    }
private:
    void write(uint32_t a, uint32_t v, uint8_t width) {
        if (a < 0x03000000u || a >= 0x04000000u ||
            diff.write_count >= kDiffMaxWrites) {
            diff.failed = true; return;
        }
        const uint32_t off = a & 0x7FFFu;
        for (uint8_t i = 0; i < width; ++i)
            diff.iwram[(off + i) & 0x7FFFu] = (uint8_t)(v >> (8u * i));
        diff.writes[diff.write_count++] = {a, v, width};
    }
    IntervalDiff& diff;
    gba::GbaBus& bus;
};

static void diff_reset(IntervalDiff& d) {
    d.active = false;
    d.ready = false;
    d.failed = false;
    d.status = nullptr;
    d.entry_pc = 0;
    d.exit_pc = 0;
    d.instructions = 0;
    d.cycles = 0;
    d.native_start_cycles = 0;
    d.native_cycles = 0;
    d.write_count = 0;
    d.native_write_count = 0;
    for (unsigned i = 0; i < kDiffMaxWrites; ++i) {
        d.writes[i] = DiffWrite{};
        d.native_writes[i] = DiffWrite{};
    }
    d.snapshot = armv4t::CPUState{};
    d.expected = armv4t::CPUState{};
    d.waits = RuntimeWaitTable{};
    d.native_exit_reason = "none";
    d.native_exit_detail[0] = '\0';
    d.native_exit_thumb = false;
    d.native_block_runs = 0;
    d.native_fallback_steps = 0;
    d.boundary_pc = 0;
    d.boundary_hw = 0;
    d.boundary_via = "none";
    d.entry_guest_cycles = 0;
    d.strh_effective_address = d.strh_value = 0;
    d.strh_access_width = 0;
    d.strh_region = "not-captured";
    d.cpu_mismatches = 0;
    d.iwram_mismatch_bytes = 0;
    d.state_match = d.cpsr_match = d.memory_match = d.cycles_match =
        d.writes_consistent = false;
    std::memset(d.iwram, 0, sizeof(d.iwram));
}

// Snapshot the live state at the entry PC. The copy is what the replay runs
// against, so the snapshot stays isolated from later native execution.
static bool diff_snapshot(IntervalDiff& d, const char* stage) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) { d.status = "BLOCKED"; return false; }
    d.active = true;
    std::memcpy(d.iwram, bus->iwram_read_ptr(), sizeof(d.iwram));
    d.waits = *bus->wait_table();
    gbarecomp::load_arm_cpu_into_interp(g_cpu, d.snapshot);
    d.expected = d.snapshot;
    d.entry_guest_cycles = g_runtime_cycles;
    std::fprintf(stderr,
        "[sma2-jit-diff] %s entry pc=%08X sp=%08X lr=%08X cpsr=%08X mode=%s\n",
        stage, (unsigned)g_cpu.R[15], (unsigned)g_cpu.R[13], (unsigned)g_cpu.R[14],
        (unsigned)g_cpu.cpsr, (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm");
    return true;
}

// Shared arming primitive for stages that compare exactly one translated
// block. Each call takes a fresh committed-state snapshot.
static bool arm_one_block_diff(IntervalDiff& d, const char* stage,
                               uint32_t entry_pc) {
    diff_reset(d);
    d.entry_pc = entry_pc;
    return diff_snapshot(d, stage);
}

// Replay the production Thumb decoder and interpreter from the snapshot. PC is
// only ever moved by the decoded instruction's own semantics, so reaching the
// target is evidence of equivalent control flow, not a forced match.
static void reference_replay(IntervalDiff& d, const char* stage, uint32_t target_pc) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) { d.status = "BLOCKED"; return; }
    IntervalShadowBus shadow(d, *bus);
    while (!d.failed && d.instructions < kDiffMaxInstructions &&
           d.expected.R[15] != target_pc) {
        const uint32_t pc = d.expected.R[15];
        if (!(pc >= 0x08000000u && pc < 0x0E000000u) ||
            !d.expected.cpsr.t) {
            d.failed = true; break;
        }
        const uint16_t hw = shadow.read16(pc);
        const armv4t::Instr insn = armv4t::ThumbDecoder::decode(hw, pc);
        uint32_t cycles = 0;
        const auto result = armv4t::Interpreter::step(
            d.expected, shadow, insn, &cycles);
        std::fprintf(stderr,
            "[sma2-jit-diff] %s interp pc=%08X hw=%04X next=%08X result=%u cyc=%u\n",
            stage, (unsigned)pc, (unsigned)hw,
            (unsigned)d.expected.R[15], (unsigned)result, (unsigned)cycles);
        if (result == armv4t::Interpreter::Result::NotImplemented ||
            result == armv4t::Interpreter::Result::Undefined ||
            result == armv4t::Interpreter::Result::Swi) {
            d.failed = true; break;
        }
        d.cycles += cycles;
        ++d.instructions;
        d.boundary_pc = pc;
        d.boundary_hw = hw;
        d.boundary_via = (result == armv4t::Interpreter::Result::Branched)
            ? "branch" : (result == armv4t::Interpreter::Result::ExceptionReturn)
            ? "spsr-return" : "sequential";
    }
    d.ready = !d.failed && d.expected.R[15] == target_pc;
    std::fprintf(stderr,
        "[sma2-jit-diff] %s reference %s entry=%08X exit=%08X insns=%u cycles=%u writes=%u arrived_via=%s\n",
        stage, d.ready ? "ready" : "failed", (unsigned)d.entry_pc,
        (unsigned)d.expected.R[15], (unsigned)d.instructions, (unsigned)d.cycles,
        (unsigned)d.write_count, d.boundary_via);
    for (uint32_t i = 0; i < d.write_count; ++i)
        std::fprintf(stderr, "[sma2-jit-diff] %s ref-write %08X/%u=%08X\n", stage,
                     (unsigned)d.writes[i].address,
                     (unsigned)d.writes[i].width,
                     (unsigned)d.writes[i].value);
}

// Observable native memory effects: live IWRAM bytes the native path changed
// relative to the snapshot. A store that rewrote the value already present is
// not observable this way, so this is a lower bound on the native write set.
static void capture_native_writes(IntervalDiff& d) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) return;
    const uint8_t* live = bus->iwram_read_ptr();
    for (uint32_t off = 0; off < 0x8000u && d.native_write_count < kDiffMaxWrites;
         off += 4u) {
        uint32_t now = 0, base = 0;
        for (unsigned i = 0; i < 4; ++i) {
            now |= (uint32_t)live[off + i] << (8u * i);
            base |= (uint32_t)d.iwram[off + i] << (8u * i);
        }
        if (now != base)
            d.native_writes[d.native_write_count++] = {0x03000000u + off, now, 4};
    }
}

static void record_exit_reason(IntervalDiff& d, const struct ARMJITStats& before,
                               const struct ARMJITStats& after) {
    bool any = false;
    char* p = d.native_exit_detail;
    for (unsigned i = 0; i < sizeof(kExitReasonNames) / sizeof(kExitReasonNames[0]); ++i) {
        const uint32_t delta = after.exits[i] - before.exits[i];
        if (!delta) continue;
        ++d.native_exit_reason_count;
        if (!any) { d.native_exit_reason = kExitReasonNames[i]; any = true; }
        p += std::snprintf(p, sizeof(d.native_exit_detail) - (size_t)(p - d.native_exit_detail),
                           "%s:+%u", kExitReasonNames[i], (unsigned)delta);
    }
    if (!any) d.native_exit_reason = "none";
}

// Stage A: the interval already validated on physical hardware. Its entry,
// exit PC, limits and comparison fields are unchanged.
static void stage_a_begin() {
    if (stage_a.active) return;
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || g_cpu.R[15] != kStageAEntryPc || !(g_cpu.cpsr & CPSR_T_BIT)) return;
    diff_reset(stage_a);
    stage_a.entry_pc = kStageAEntryPc;
    stage_a.exit_pc = kStageAExitPc;
    if (!diff_snapshot(stage_a, "stage1")) return;
    reference_replay(stage_a, "stage1", kStageAExitPc);
}

static const char* address_region(uint32_t address) {
    if (address >= 0x03000000u && address < 0x04000000u) return "iwram";
    if (address < 0x00004000u) return "bios";
    if (address >= 0x02000000u && address < 0x03000000u) return "ewram";
    if (address >= 0x04000000u && address < 0x05000000u) return "io";
    if (address >= 0x05000000u && address < 0x07000000u) return "palette-vram-oam";
    if (address >= 0x08000000u && address < 0x0E000000u) return "rom";
    return "unsupported";
}

// Run only the harmless instruction prefix privately, stopping before STRH.
// This derives the effective address from interpreter state at the store
// instruction and never accesses live data memory or executes the store.
static bool preflight_iwram_strh(IntervalDiff& d, const char* stage,
                                 uint32_t strh_pc) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) { d.status = "BLOCKED"; return false; }
    armv4t::CPUState probe = d.snapshot;
    IntervalShadowBus shadow(d, *bus);
    unsigned steps = 0;
    while (probe.R[15] != strh_pc && steps++ < 4) {
        if (!probe.cpsr.t || probe.R[15] < 0x08000000u ||
            probe.R[15] >= 0x0E000000u) break;
        const uint32_t pc = probe.R[15];
        const uint16_t hw = shadow.read16(pc);
        const armv4t::Instr insn = armv4t::ThumbDecoder::decode(hw, pc);
        uint32_t cycles = 0;
        const auto result = armv4t::Interpreter::step(probe, shadow, insn, &cycles);
        if (result != armv4t::Interpreter::Result::Normal || d.write_count)
            break;
    }
    if (d.failed || probe.R[15] != strh_pc || !probe.cpsr.t) {
        d.status = "INELIGIBLE";
        d.strh_region = "preflight-failed";
        return false;
    }
    const uint16_t hw = shadow.read16(strh_pc);
    const armv4t::Instr insn = armv4t::ThumbDecoder::decode(hw, strh_pc);
    if (insn.op != armv4t::IrOp::STRH || insn.mem.by_register ||
        insn.rd >= 16 || insn.mem.rn >= 16) {
        d.status = "INELIGIBLE";
        d.strh_region = "unexpected-instruction";
        return false;
    }
    const uint32_t base = probe.R[insn.mem.rn];
    const uint32_t address = insn.mem.pre_indexed
        ? (insn.mem.add ? base + insn.mem.imm_offset : base - insn.mem.imm_offset)
        : base;
    d.strh_effective_address = address;
    d.strh_value = probe.R[insn.rd] & 0xFFFFu;
    d.strh_access_width = 2;
    d.strh_region = address_region(address);
    std::fprintf(stderr,
        "[sma2-jit-diff] %s STRH pc=%08X address=%08X value=%04X width=2 region=%s\n",
        stage, (unsigned)strh_pc, (unsigned)address, (unsigned)d.strh_value,
        d.strh_region);
    if (address < 0x03000000u || address >= 0x04000000u ||
        address + 1u >= 0x04000000u) {
        d.status = "INELIGIBLE";
        return false;
    }
    return true;
}

// Stage E begins with a decoded word store. Validate its target against the
// private shadow-IWRAM window before allowing native execution, so an
// unexpected live stack cannot make the diagnostic perform an unsupported
// external write. Address calculation uses the production decoder and the
// fresh entry snapshot; no live data-memory access is performed.
static bool preflight_iwram_store(IntervalDiff& d, const char* stage,
                                 uint32_t store_pc) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) { d.status = "BLOCKED"; return false; }
    if (!d.snapshot.cpsr.t || d.snapshot.R[15] != store_pc ||
        store_pc < 0x08000000u || store_pc >= 0x0E000000u) {
        d.status = "INELIGIBLE";
        return false;
    }
    IntervalShadowBus shadow(d, *bus);
    const uint16_t hw = shadow.read16(store_pc);
    const armv4t::Instr insn = armv4t::ThumbDecoder::decode(hw, store_pc);
    if (d.failed || insn.op != armv4t::IrOp::STR || insn.mem.byte_access ||
        insn.mem.by_register || insn.rd >= 16 || insn.mem.rn >= 16 ||
        insn.mem.writeback) {
        d.status = "INELIGIBLE";
        return false;
    }
    const uint32_t base = d.snapshot.R[insn.mem.rn];
    const uint32_t address = insn.mem.pre_indexed
        ? (insn.mem.add ? base + insn.mem.imm_offset
                        : base - insn.mem.imm_offset)
        : base;
    const uint32_t width = insn.mem.byte_access ? 1u : 4u;
    const char* region = address_region(address);
    std::fprintf(stderr,
        "[sma2-jit-diff] %s store preflight pc=%08X hw=%04X "
        "address=%08X value=%08X width=%u region=%s\n",
        stage, (unsigned)store_pc, (unsigned)hw, (unsigned)address,
        (unsigned)d.snapshot.R[insn.rd], (unsigned)width, region);
    if (address < 0x03000000u || address >= 0x04000000u ||
        width > 0x04000000u - address) {
        d.status = "INELIGIBLE";
        return false;
    }
    return true;
}

static const char* g_stop_condition = "not-run";
static uint32_t armcore_guest_pc();

static bool diff_compare(IntervalDiff& d, const char* stage,
                         uint32_t native_cycle_delta, bool check_write_sets,
                         bool byte_write_compare = false) {
    if (!d.ready) {
        if (!d.status) d.status = "INELIGIBLE";
        return false;
    }
    armv4t::CPUState actual{};
    gbarecomp::load_arm_cpu_into_interp(g_cpu, actual);
    bool state_match = true;
    for (unsigned i = 0; i < 16; ++i) {
        if (actual.R[i] != d.expected.R[i]) { state_match = false; ++d.cpu_mismatches; }
    }
    for (unsigned i = 0; i < armv4t::Bank_Count; ++i) {
        if (actual.banked_sp[i] != d.expected.banked_sp[i] ||
            actual.banked_lr[i] != d.expected.banked_lr[i] ||
            actual.banked_spsr[i] != d.expected.banked_spsr[i]) {
            state_match = false;
            ++d.cpu_mismatches;
            std::fprintf(stderr,
                "[sma2-jit-diff] %s bank[%u] sp=%08X/%08X lr=%08X/%08X spsr=%08X/%08X\n",
                stage, i, (unsigned)actual.banked_sp[i],
                (unsigned)d.expected.banked_sp[i], (unsigned)actual.banked_lr[i],
                (unsigned)d.expected.banked_lr[i], (unsigned)actual.banked_spsr[i],
                (unsigned)d.expected.banked_spsr[i]);
        }
    }
    for (unsigned i = 0; i < 5; ++i) {
        if (actual.r8_12_user[i] != d.expected.r8_12_user[i] ||
            actual.r8_12_fiq[i] != d.expected.r8_12_fiq[i]) {
            state_match = false;
            ++d.cpu_mismatches;
            std::fprintf(stderr,
                "[sma2-jit-diff] %s banked-r%u user=%08X/%08X fiq=%08X/%08X\n",
                stage, i + 8, (unsigned)actual.r8_12_user[i],
                (unsigned)d.expected.r8_12_user[i], (unsigned)actual.r8_12_fiq[i],
                (unsigned)d.expected.r8_12_fiq[i]);
        }
    }
    const auto& a = actual.cpsr;
    const auto& e = d.expected.cpsr;
    const bool cpsr_match = a.n == e.n && a.z == e.z && a.c == e.c &&
        a.v == e.v && a.i == e.i && a.f == e.f && a.t == e.t && a.mode == e.mode;
    gba::GbaBus* bus = gbarecomp::active_bus();
    const bool memory_match = bus && std::memcmp(
        bus->iwram_read_ptr(), d.iwram, sizeof(d.iwram)) == 0;
    if (bus) {
        const uint8_t* live = bus->iwram_read_ptr();
        for (unsigned i = 0; i < sizeof(d.iwram); ++i)
            if (live[i] != d.iwram[i]) {
                ++d.iwram_mismatch_bytes;
                if (d.iwram_mismatch_bytes <= 16)
                    std::fprintf(stderr,
                        "[sma2-jit-diff] %s IWRAM %08X jit=%02X interp=%02X\n",
                        stage, 0x03000000u + i, (unsigned)live[i],
                        (unsigned)d.iwram[i]);
            }
    }
    const bool cycles_match = native_cycle_delta == d.cycles;
    d.writes_consistent = true;
    if (check_write_sets) {
        for (uint32_t i = 0; i < d.native_write_count; ++i) {
            if (byte_write_compare) {
                const uint32_t off = d.native_writes[i].address - 0x03000000u;
                const uint32_t now = d.native_writes[i].value;
                uint32_t base = 0;
                for (unsigned b = 0; b < 4; ++b)
                    base |= (uint32_t)d.iwram[(off + b) & 0x7FFFu] << (8u * b);
                for (unsigned b = 0; b < 4; ++b) {
                    const uint8_t old_byte = (uint8_t)(base >> (8u * b));
                    const uint8_t new_byte = (uint8_t)(now >> (8u * b));
                    if (old_byte == new_byte) continue;
                    bool found_byte = false;
                    const uint32_t address = d.native_writes[i].address + b;
                    for (uint32_t j = 0; j < d.write_count; ++j) {
                        const DiffWrite& w = d.writes[j];
                        if (address >= w.address && address < w.address + w.width &&
                            (uint8_t)(w.value >> (8u * (address - w.address))) == new_byte) {
                            found_byte = true;
                            break;
                        }
                    }
                    if (!found_byte) d.writes_consistent = false;
                }
                continue;
            }
            bool found = false;
            for (uint32_t j = 0; j < d.write_count; ++j) {
                if (d.writes[j].address == d.native_writes[i].address &&
                    d.writes[j].value == d.native_writes[i].value) {
                    found = true; break;
                }
            }
            if (!found) { d.writes_consistent = false; break; }
        }
    }
    d.state_match = state_match;
    d.cpsr_match = cpsr_match;
    d.memory_match = memory_match;
    d.cycles_match = cycles_match;
    const bool pass = state_match && cpsr_match && memory_match && cycles_match &&
                      (!check_write_sets || d.writes_consistent);
    d.status = pass ? "PASS" : "MISMATCH";
    std::fprintf(stderr,
        "[sma2-jit-diff] %s result %s state=%s cpsr=%s iwram=%s cycles=%s native=%u interp=%u insns=%u\n",
        stage, pass ? "MATCH" : "MISMATCH", state_match ? "match" : "DIFF",
        cpsr_match ? "match" : "DIFF", memory_match ? "match" : "DIFF",
        cycles_match ? "match" : "DIFF", (unsigned)native_cycle_delta,
        (unsigned)d.cycles, (unsigned)d.instructions);
    for (unsigned i = 0; i < 16; ++i) {
        if (actual.R[i] != d.expected.R[i])
            std::fprintf(stderr, "[sma2-jit-diff] %s R%u jit=%08X interp=%08X\n", stage,
                         i, (unsigned)actual.R[i], (unsigned)d.expected.R[i]);
    }
    if (!cpsr_match)
        std::fprintf(stderr,
            "[sma2-jit-diff] %s CPSR jit=%u%u%u%u%u%u%u/%02X "
            "interp=%u%u%u%u%u%u%u/%02X\n", stage,
            (unsigned)a.n, (unsigned)a.z, (unsigned)a.c, (unsigned)a.v,
            (unsigned)a.i, (unsigned)a.f, (unsigned)a.t, (unsigned)a.mode,
            (unsigned)e.n, (unsigned)e.z, (unsigned)e.c, (unsigned)e.v,
            (unsigned)e.i, (unsigned)e.f, (unsigned)e.t, (unsigned)e.mode);
    if (!memory_match && d.iwram_mismatch_bytes > 16)
        std::fprintf(stderr,
            "[sma2-jit-diff] %s IWRAM additional differing bytes=%u (first 16 listed)\n",
            stage, (unsigned)(d.iwram_mismatch_bytes - 16));
    if (check_write_sets) {
        for (uint32_t i = 0; i < d.native_write_count; ++i)
            std::fprintf(stderr, "[sma2-jit-diff] %s native-write %08X=%08X\n", stage,
                         (unsigned)d.native_writes[i].address,
                         (unsigned)d.native_writes[i].value);
    }
    return pass;
}

// Shared per-run capture and one-block gate used by stages B–D.
static void record_one_block_exit(IntervalDiff& d,
                                  const struct ARMJITStats& before,
                                  const struct ARMJITStats& after) {
    d.native_block_runs = (uint32_t)(after.blockRuns - before.blockRuns);
    d.native_fallback_steps =
        (uint32_t)(after.fallbackSteps - before.fallbackSteps);
    record_exit_reason(d, before, after);
    d.exit_pc = armcore_guest_pc();
    d.native_exit_thumb = (cpu.cpsr.packed & CPSR_T_BIT) != 0;
}

static bool one_block_eligible(IntervalDiff& d, const char* stage,
                               bool reject_zero_progress_event) {
    const char* failure = nullptr;
    if (jit_illegal_seen) {
        d.status = "INELIGIBLE";
        failure = "unsupported opcode in one-block comparison";
    } else if (d.native_block_runs != 1 || d.native_fallback_steps != 0) {
        d.status = "INELIGIBLE";
        failure = "one-block comparison did not run exactly one native block without fallback";
    } else if (d.native_cycles == 0 ||
               (reject_zero_progress_event && d.exit_pc == d.entry_pc &&
                std::strcmp(d.native_exit_reason, "event") == 0)) {
        d.status = "INELIGIBLE";
        failure = "one-block comparison made no eligible native progress";
    }
    if (failure) g_stop_condition = failure;
    std::fprintf(stderr,
        "[sma2-jit-diff] %s eligibility=%s native entry=%08X/%s exit=%08X/%s reason=%s "
        "block_runs=%u fallback=%u cycles=%u unsupported=%s\n",
        stage, failure ? "INELIGIBLE" : "eligible", (unsigned)d.entry_pc,
        d.snapshot.cpsr.t ? "thumb" : "arm",
        (unsigned)d.exit_pc, d.native_exit_thumb ? "thumb" : "arm",
        d.native_exit_reason, (unsigned)d.native_block_runs,
        (unsigned)d.native_fallback_steps, (unsigned)d.native_cycles,
        jit_illegal_seen ? "yes" : "no");
    return d.status == nullptr;
}

static void compare_one_block(IntervalDiff& d, const char* stage,
                              bool check_write_sets, bool byte_write_compare,
                              bool require_branch_boundary) {
    if (d.status) return;
    capture_native_writes(d);
    reference_replay(d, stage, d.exit_pc);
    if (!d.ready) {
        d.status = "INELIGIBLE";
        g_stop_condition = "one-block interpreter replay could not establish the native exit boundary";
        return;
    }
    if (require_branch_boundary &&
        (std::strcmp(d.boundary_via, "branch") != 0 ||
         (std::strcmp(d.native_exit_reason, "site_branch") != 0 &&
          std::strcmp(d.native_exit_reason, "branch") != 0))) {
        d.status = "INELIGIBLE";
        std::fprintf(stderr,
            "[sma2-jit-diff] %s ambiguous boundary replay_via=%s native_reason=%s\n",
            stage, d.boundary_via, d.native_exit_reason);
        return;
    }
    diff_compare(d, stage, d.native_cycles, check_write_sets,
                 byte_write_compare);
    if (d.status && std::strcmp(d.status, "MISMATCH") == 0)
        g_stop_condition = "one-block interpreter/native state, memory, or cycle mismatch";
}

static void print_stage(const char* stage, const IntervalDiff& d) {
    std::fprintf(stderr,
        "[sma2-jit-diff] %s status=%s entry=%08X/%s exit=%08X/%s exit_reason=%s "
        "blocks=%u fallback=%u insns=%u interp_cycles=%u native_cycles=%u "
        "mismatches=%u cpsr=%s iwram=%s "
        "writes interp=%u native=%u consistent=%s iwram_diff_bytes=%u\n",
        stage, d.status ? d.status : "not-run", (unsigned)d.entry_pc,
        d.snapshot.cpsr.t ? "thumb" : "arm", (unsigned)d.exit_pc,
        d.native_exit_thumb ? "thumb" : "arm",
        d.native_exit_reason, (unsigned)d.native_block_runs,
        (unsigned)d.native_fallback_steps, (unsigned)d.instructions,
        (unsigned)d.cycles, (unsigned)d.native_cycles,
        (unsigned)d.cpu_mismatches, d.cpsr_match ? "match" : "DIFF",
        d.memory_match ? "match" : "DIFF", (unsigned)d.write_count,
        (unsigned)d.native_write_count, d.writes_consistent ? "yes" : "no",
        (unsigned)d.iwram_mismatch_bytes);
}

static bool summary_printed;

// Count every outer runner step, including steps routed directly to the
// production interpreter. Counting only drive_dynarec() calls would leave
// interpreter-only paths outside the diagnostic bound.
static bool diagnostic_step() {
    if (diagnostic_finished) return true;
    ++diagnostic_steps;
    if (first_div_waiting_post_e_swi || first_div_active) {
        ++first_div_outer_steps;
        if (diagnostic_steps > kMaxDiagnosticSteps ||
            first_div_outer_steps > kFirstDivMaxOuterSteps) {
            first_div_status = first_div_waiting_post_e_swi
                ? "NOT_REACHED" : "INELIGIBLE";
            first_div_stop_reason = first_div_waiting_post_e_swi
                ? "post-E SWI return boundary not observed within budget"
                : "outer-step budget exhausted during bounded block sequence";
            g_stop_condition = first_div_stop_reason;
            diagnostic_finished = true;
        } else if (first_div_waiting_post_e_swi) {
            const uint32_t pc = g_cpu.R[15];
            const bool in_cart = pc >= 0x08000000u && pc < 0x0E000000u;
            const bool in_post_e_swi = pc >= 0x0809DFBCu && pc < 0x0809DFC0u;
            if (in_cart && !in_post_e_swi) {
                first_div_waiting_post_e_swi = false;
                if (g_cpu.cpsr & CPSR_T_BIT) {
                    first_div_active = true;
                    first_div_status = "ACTIVE";
                    first_div_stop_reason = "observed first committed post-SWI ROM boundary";
                    g_stop_condition = first_div_stop_reason;
                    std::fprintf(stderr,
                        "[sma2-jit-diff] firstdiv start observed committed "
                        "entry=%08X/thumb after unmeasured SWI #0x0C path\n",
                        (unsigned)pc);
                } else {
                    first_div_status = "INELIGIBLE";
                    first_div_stop_reason = "post-SWI ROM boundary is ARM state; replay supports Thumb only";
                    g_stop_condition = first_div_stop_reason;
                    diagnostic_finished = true;
                }
            }
        } else if (first_div_active) {
            const uint32_t pc = g_cpu.R[15];
            if (!(g_cpu.cpsr & CPSR_T_BIT) || pc < 0x08000000u ||
                pc >= 0x0E000000u) {
                first_div_status = "INELIGIBLE";
                first_div_stop_reason = "next committed boundary is outside supported Thumb ROM replay";
                g_stop_condition = first_div_stop_reason;
                diagnostic_finished = true;
            }
        }
    } else if (!stage_a_completed) {
        if (diagnostic_steps > kMaxDiagnosticSteps) {
            stage_a.status = "INELIGIBLE";
            g_stop_condition = "stage1 entry boundary not reached within the runner-step budget";
            diagnostic_finished = true;
        }
    } else if (!stage_b_completed) {
        ++stage_b_steps;
        if (diagnostic_steps > kMaxDiagnosticSteps ||
            stage_b_steps > kStageBMaxSteps) {
            stage_b.status = "INELIGIBLE";
            g_stop_condition = "stage2 entry boundary not reached within the runner-step budget";
            diagnostic_finished = true;
        }
    } else if (!stage_c_armed) {
        ++stage_c_steps;
        if (diagnostic_steps > kMaxDiagnosticSteps ||
            stage_c_steps > kStageCMaxSteps) {
            stage_c.status = "NOT_REACHED";
            g_stop_condition = "stage3 entry 080015FA not reached within finite runner-step budget";
            diagnostic_finished = true;
        }
    } else if (stage_c_completed && !stage_d_armed) {
        ++stage_d_steps;
        if (diagnostic_steps > kMaxDiagnosticSteps ||
            stage_d_steps > kStageDMaxSteps) {
            stage_d.status = "NOT_REACHED";
            g_stop_condition = "stageD entry 0800160A not observed within the bounded post-SWI continuation";
            diagnostic_finished = true;
        }
    } else if (stage_d_completed && !stage_e_armed) {
        ++stage_e_steps;
        if (diagnostic_steps > kMaxDiagnosticSteps ||
            stage_e_steps > kStageEMaxSteps) {
            stage_e.status = "NOT_REACHED";
            g_stop_condition = "stageE entry 0800161A not observed within the bounded post-D SWI continuation";
            diagnostic_finished = true;
        }
    }
    return diagnostic_finished;
}

// Stage B classification and reporting. The native block has already run, so
// its observable memory effects are captured before the replay mutates the
// private IWRAM copy.
static void stage_b_finish() {
    if (!stage_b.status) {
        compare_one_block(stage_b, "stage2", true, false, false);
    }
    stage_b_completed = stage_b.status && std::strcmp(stage_b.status, "PASS") == 0;
    if (stage_b_completed) {
        g_stop_condition = "stage2 passed; runner resumed for the single planned stage3 advancement";
        std::fprintf(stderr,
            "[sma2-jit-diff] stage2 PASS: outer runner may resume for stage3\n");
    } else {
        diagnostic_finished = true;
    }
}

static void stage_c_finish() {
    if (!stage_c.status) {
        compare_one_block(stage_c, "stage3", true, true, false);
    }
    stage_c_completed = stage_c.status && std::strcmp(stage_c.status, "PASS") == 0;
    if (stage_c_completed) {
        g_stop_condition = "stage3 passed; bounded continuation allowed to observe stageD entry";
        std::fprintf(stderr,
            "[sma2-jit-diff] stage3 PASS: bounded continuation may observe stageD\n");
    } else {
        g_stop_condition = "stage3 terminated without PASS";
        diagnostic_finished = true;
    }
}

static void stage_d_finish() {
    if (!stage_d.status)
        compare_one_block(stage_d, "stageD", true, true, true);
    if (!stage_d.status) {
        stage_d.status = "INELIGIBLE";
        g_stop_condition = "stageD comparison did not produce a classification";
    }
    stage_d_completed = std::strcmp(stage_d.status, "PASS") == 0;
    if (stage_d_completed) {
        g_stop_condition = "stageD passed; bounded continuation allowed to observe stageE entry";
        std::fprintf(stderr,
            "[sma2-jit-diff] stageD PASS: bounded continuation may observe stageE\n");
    } else {
        if (std::strcmp(g_stop_condition,
                "stage3 passed; bounded continuation allowed to observe stageD entry") == 0)
            g_stop_condition = "stageD replay or native eligibility failed";
        diagnostic_finished = true;
    }
}

static void stage_e_finish() {
    if (!stage_e.status)
        compare_one_block(stage_e, "stageE", true, true, true);
    if (!stage_e.status) {
        stage_e.status = "INELIGIBLE";
        g_stop_condition = "stageE comparison did not produce a classification";
    }
    if (std::strcmp(stage_e.status, "PASS") == 0) {
        g_stop_condition = "stageE comparison complete";
        first_div_waiting_post_e_swi = true;
        first_div_status = "WAITING_POST_E_SWI";
        first_div_stop_reason = "waiting for committed ROM return after SWI #0x0C";
        std::fprintf(stderr,
            "[sma2-jit-diff] firstdiv waiting for Stage E SWI return; "
            "SWI/runtime behavior is outside comparison\n");
        return;
    } else if (std::strcmp(g_stop_condition,
                "stageD passed; bounded continuation allowed to observe stageE entry") == 0)
        g_stop_condition = "stageE replay or native eligibility failed";
    diagnostic_finished = true;
}

static void print_first_div_summary() {
    std::fprintf(stderr,
        "[sma2-jit-diff] firstdiv status=%s attempts=%u/%u passes=%u/%u "
        "outer_steps=%u/%u transition_runs=%u/%u stop=%s\n",
        first_div_status, first_div_attempts, kFirstDivMaxBlocks,
        first_div_passes, kFirstDivMaxBlocks, first_div_outer_steps,
        kFirstDivMaxOuterSteps, first_div_transition_runs,
        kFirstDivMaxTransitionRuns, first_div_stop_reason);
    for (unsigned i = 0; i < first_div_attempts; ++i) {
        const FirstDivBlockRecord& r = first_div_blocks[i];
        std::fprintf(stderr,
            "[sma2-jit-diff] firstdiv block%u status=%s entry=%08X/%s "
            "exit=%08X/%s reason=%s blocks=%u fallback=%u insns=%u "
            "native_cycles=%u interp_cycles=%u mismatches=%u\n",
            i + 1, r.status, (unsigned)r.entry_pc,
            r.entry_thumb ? "thumb" : "arm", (unsigned)r.exit_pc,
            r.exit_thumb ? "thumb" : "arm", r.exit_reason,
            (unsigned)r.block_runs, (unsigned)r.fallback_steps,
            (unsigned)r.instructions, (unsigned)r.native_cycles,
            (unsigned)r.interp_cycles, (unsigned)r.mismatches);
    }
}

static void first_div_finish_block() {
    if (!first_div_single_block) return;
    if (first_div_attempts >= kFirstDivMaxBlocks) {
        first_div_status = "INELIGIBLE";
        first_div_stop_reason = "four-block comparison limit reached";
        diagnostic_finished = true;
        return;
    }
    const char* label = first_div_attempts == 0 ? "firstdiv[1]" :
        first_div_attempts == 1 ? "firstdiv[2]" :
        first_div_attempts == 2 ? "firstdiv[3]" : "firstdiv[4]";
    FirstDivBlockRecord& record = first_div_blocks[first_div_attempts++];
    if (!first_div_diff.status)
        compare_one_block(first_div_diff, label, true, true, true);
    record.status = first_div_diff.status ? first_div_diff.status : "INELIGIBLE";
    record.entry_pc = first_div_diff.entry_pc;
    record.exit_pc = first_div_diff.exit_pc;
    record.entry_thumb = first_div_diff.snapshot.cpsr.t;
    record.exit_thumb = first_div_diff.native_exit_thumb;
    record.exit_reason = first_div_diff.native_exit_reason;
    record.block_runs = first_div_diff.native_block_runs;
    record.fallback_steps = first_div_diff.native_fallback_steps;
    record.instructions = first_div_diff.instructions;
    record.native_cycles = first_div_diff.native_cycles;
    record.interp_cycles = first_div_diff.cycles;
    record.mismatches = first_div_diff.cpu_mismatches +
        first_div_diff.iwram_mismatch_bytes +
        (first_div_diff.cpsr_match ? 0u : 1u) +
        (first_div_diff.cycles_match ? 0u : 1u);
    first_div_single_block = false;

    if (std::strcmp(record.status, "PASS") == 0) {
        ++first_div_passes;
        if (first_div_passes == kFirstDivMaxBlocks) {
            first_div_status = "PASS";
            first_div_stop_reason = "four eligible block boundaries matched";
            g_stop_condition = first_div_stop_reason;
            diagnostic_finished = true;
        } else {
            first_div_status = "ACTIVE";
            first_div_stop_reason = "block matched; continue at next committed boundary";
            g_stop_condition = first_div_stop_reason;
        }
    } else {
        first_div_status = record.status;
        first_div_stop_reason = std::strcmp(record.status, "MISMATCH") == 0
            ? "first observed block-boundary mismatch"
            : "first candidate violated comparison eligibility";
        g_stop_condition = first_div_stop_reason;
        diagnostic_finished = true;
    }
}

void print_final_summary() {
    // Printed once, from the runner gate after it stops the outer game runner.
    if (summary_printed) return;
    summary_printed = true;
    std::fprintf(stderr, "[sma2-jit-diff] === final diagnostic summary ===\n");
    print_stage("stage1", stage_a);
    print_stage("stage2", stage_b);
    print_stage("stage3", stage_c);
    print_stage("stageD", stage_d);
    print_stage("stageE", stage_e);
    print_first_div_summary();
    std::fprintf(stderr,
        "[sma2-jit-diff] stage2 traversal budget steps=%u/%u block_runs=%u/%u\n",
        (unsigned)stage_b_steps, (unsigned)kStageBMaxSteps,
        (unsigned)stage_b_block_runs, (unsigned)kStageBMaxBlockRuns);
    std::fprintf(stderr,
        "[sma2-jit-diff] stage3 traversal steps=%u/%u block_runs=%u/%u entry_cycles=%llu "
        "strh_pc=%08X strh_address=%08X value=%04X width=%u region=%s\n",
        (unsigned)stage_c_steps, (unsigned)kStageCMaxSteps,
        (unsigned)stage_c_block_runs, (unsigned)kStageCMaxBlockRuns,
        (unsigned long long)stage_c.entry_guest_cycles,
        (unsigned)kStageCStrhPc,
        (unsigned)stage_c.strh_effective_address, (unsigned)stage_c.strh_value,
        (unsigned)stage_c.strh_access_width, stage_c.strh_region);
    std::fprintf(stderr,
        "[sma2-jit-diff] stageD traversal steps=%u/%u block_runs=%u/%u entry_cycles=%llu "
        "strh_pc=%08X strh_address=%08X value=%04X width=%u region=%s\n",
        (unsigned)stage_d_steps, (unsigned)kStageDMaxSteps,
        (unsigned)stage_d_block_runs, (unsigned)kStageDMaxBlockRuns,
        (unsigned long long)stage_d.entry_guest_cycles,
        (unsigned)kStageDStrhPc,
        (unsigned)stage_d.strh_effective_address, (unsigned)stage_d.strh_value,
        (unsigned)stage_d.strh_access_width, stage_d.strh_region);
    std::fprintf(stderr,
        "[sma2-jit-diff] stageE traversal steps=%u/%u block_runs=%u/%u entry_cycles=%llu\n",
        (unsigned)stage_e_steps, (unsigned)kStageEMaxSteps,
        (unsigned)stage_e_block_runs, (unsigned)kStageEMaxBlockRuns,
        (unsigned long long)stage_e.entry_guest_cycles);
    std::fprintf(stderr, "[sma2-jit-diff] stop condition: %s\n", g_stop_condition);
}


// ── Mode / bank translation ──────────────────────────────────────────
// arm.h's enum PrivilegeMode already uses the ARM ARM encodings (0x10 user,
// 0x11 FIQ, 0x12 IRQ, 0x13 supervisor, 0x17 abort, 0x1B undefined, 0x1F
// system), and arm.c's ARMSelectBank maps those to the dense RegisterBank
// indices (BANK_NONE=0, FIQ=1, IRQ=2, SUPERVISOR=3, ABORT=4, UNDEFINED=5).
// GBARecomp's mode_to_bank (runtime_arm.cpp:1083) uses the same dense
// indices for the same 5-bit modes, so the mapping is the identity and the
// bank arrays line up slot for slot.
static unsigned sma2_bank_of(uint32_t cpsr) {
    switch (cpsr & 0x1Fu) {
        case 0x11u: return BANK_FIQ;
        case 0x12u: return BANK_IRQ;
        case 0x13u: return BANK_SUPERVISOR;
        case 0x17u: return BANK_ABORT;
        case 0x1Bu: return BANK_UNDEFINED;
        default: return BANK_NONE;  // user 0x10 / system 0x1F
    }
}

// ── State bridge: ArmCpuState (GBARecomp) <-> struct ARMCore (mGBA) ──
//
// Both keep the active window in R[]/gprs[] and the inactive bank in a
// dense-indexed array. Two conventions differ and are handled explicitly:
//
//   * SPSR. GBARecomp stores the CURRENT mode's SPSR in
//     banked_spsr[mode_to_bank(cpsr)] (runtime_mrs_spsr reads that slot).
//     mGBA keeps the current SPSR in cpu.spsr and leaves
//     bankedSPSRs[ARMSelectBank(privilegeMode)] stale (arm.c:16
//     ARMSetPrivilegeMode moves bankedSPSRs[old] -> spsr and back).
//   * R8..R12. GBARecomp splits them into r8_12_user/r8_12_fiq plus the
//     active R[8..12]; mGBA uses bankedRegisters[BANK_NONE][2..6] and
//     bankedRegisters[BANK_FIQ][2..6] plus the active gprs[8..12].
static void armcore_load_from_g_cpu() {
    for (int i = 0; i < 16; ++i) cpu.gprs[i] = (int32_t)g_cpu.R[i];
    cpu.cpsr.packed = (int32_t)g_cpu.cpsr;
    const unsigned bank = sma2_bank_of(g_cpu.cpsr);
    // enum PrivilegeMode is the 5-bit mode itself (arm.h:29).
    cpu.privilegeMode = (PrivilegeMode)(g_cpu.cpsr & 0x1Fu);
    cpu.executionMode = (g_cpu.cpsr & CPSR_T_BIT) ? MODE_THUMB : MODE_ARM;
    for (int b = 0; b < 6; ++b) {
        cpu.bankedRegisters[b][0] = (int32_t)g_cpu.banked_sp[b];
        cpu.bankedRegisters[b][1] = (int32_t)g_cpu.banked_lr[b];
        cpu.bankedSPSRs[b] = (int32_t)g_cpu.banked_spsr[b];
    }
    for (int i = 0; i < 5; ++i) {
        cpu.bankedRegisters[BANK_NONE][2 + i] = (int32_t)g_cpu.r8_12_user[i];
        cpu.bankedRegisters[BANK_FIQ][2 + i] = (int32_t)g_cpu.r8_12_fiq[i];
    }
    // mGBA's live SPSR slot, from GBARecomp's current-bank slot.
    cpu.spsr.packed = (int32_t)g_cpu.banked_spsr[bank];
    cpu.cycles = 0;
    cpu.halted = 0;
}

static void armcore_store_into_g_cpu() {
    for (int i = 0; i < 16; ++i) g_cpu.R[i] = (uint32_t)cpu.gprs[i];
    g_cpu.cpsr = (uint32_t)cpu.cpsr.packed;
    const unsigned bank = sma2_bank_of(cpu.cpsr.packed);
    for (int b = 0; b < 6; ++b) {
        g_cpu.banked_sp[b] = (uint32_t)cpu.bankedRegisters[b][0];
        g_cpu.banked_lr[b] = (uint32_t)cpu.bankedRegisters[b][1];
        g_cpu.banked_spsr[b] = (uint32_t)cpu.bankedSPSRs[b];
    }
    // GBARecomp's current-mode SPSR slot is the live one, not mGBA's stale
    // bankedSPSRs[bank].
    g_cpu.banked_spsr[bank] = (uint32_t)cpu.spsr.packed;
    for (int i = 0; i < 5; ++i) {
        g_cpu.r8_12_user[i] = (uint32_t)cpu.bankedRegisters[BANK_NONE][2 + i];
        g_cpu.r8_12_fiq[i] = (uint32_t)cpu.bankedRegisters[BANK_FIQ][2 + i];
    }
}

// mGBA's PC convention is "address of the instruction about to execute + its
// width" (arm.c:212 ARMStep takes the opcode from prefetch[0] and only then
// advances gprs[ARM_PC]); GBARecomp's R[15] is the address of the instruction
// about to execute. jit.c:394 ARMJITStaleFlags uses the same offset.
static uint32_t armcore_guest_pc() {
    return (uint32_t)cpu.gprs[ARM_PC] -
           ((cpu.cpsr.packed & CPSR_T_BIT) ? 2u : 4u);
}

// jit.c:662 _arrive loads prefetch[0]/prefetch[1] at the guest PC and sets
// gprs[ARM_PC] = guest_pc + width. GBARecomp has no ARM prefetch state, so
// re-issuing these two fetches is exactly what its interpreter does.
static void armcore_set_pc_from_guest(uint32_t guest_pc, int thumb) {
    if (thumb) {
        cpu.prefetch[0] = bus_read_u16(guest_pc);
        cpu.prefetch[1] = bus_read_u16(guest_pc + 2u);
        cpu.gprs[ARM_PC] = (int32_t)(guest_pc + 2u);
    } else {
        cpu.prefetch[0] = bus_read_u32(guest_pc);
        cpu.prefetch[1] = bus_read_u32(guest_pc + 4u);
        cpu.gprs[ARM_PC] = (int32_t)(guest_pc + 4u);
    }
}

// Diagnostic-only, direct RAM peek for the first Thumb PUSH. Avoid the bus
// bridge here: its reads update open-bus/idle bookkeeping even for RAM.
static bool sma2_debug_read_ram32(uint32_t addr, uint32_t* value) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || (addr & 3u)) return false;
    const uint8_t* p = nullptr;
    if (addr >= 0x02000000u && addr <= 0x0203FFFCu) {
        p = bus->ewram_ptr() + (addr - 0x02000000u);
    } else if (addr >= 0x03000000u && addr <= 0x03007FFCu) {
        p = bus->iwram_read_ptr() + (addr - 0x03000000u);
    } else {
        return false;
    }
    *value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return true;
}

static void sma2_trace_push_core(const char* phase, const struct ARMCore* c,
                                uint32_t original_sp) {
    const uint32_t first = original_sp - 20u;
    std::fprintf(stderr,
        "[sma2-jit-push] %s pc=%08X mode=%s cycles=%d nextEvent=%d "
        "r13=%08X r14=%08X cpsr=%08X r4-r7=%08X,%08X,%08X,%08X "
        "prefetch=%08X stack=",
        phase, (unsigned)((uint32_t)c->gprs[ARM_PC] -
                          ((c->cpsr.packed & CPSR_T_BIT) ? 2u : 4u)),
        c->executionMode == MODE_THUMB ? "thumb" : "arm", (int)c->cycles,
        (int)c->nextEvent, (unsigned)c->gprs[ARM_SP], (unsigned)c->gprs[ARM_LR],
        (unsigned)c->cpsr.packed, (unsigned)c->gprs[4], (unsigned)c->gprs[5],
        (unsigned)c->gprs[6], (unsigned)c->gprs[7], (unsigned)c->prefetch[0]);
    if (original_sp >= 20u && !(original_sp & 3u)) {
        for (unsigned i = 0; i < 5; ++i) {
            uint32_t word = 0;
            if (!sma2_debug_read_ram32(first + 4u * i, &word)) {
                std::fprintf(stderr, "unavailable");
                break;
            }
            std::fprintf(stderr, "%s%08X:%08X", i ? "," : "",
                         (unsigned)(first + 4u * i), (unsigned)word);
        }
    } else {
        std::fprintf(stderr, "unavailable");
    }
    std::fprintf(stderr, "\n");
}

extern "C" void sma2_dc_jit_trace_push_runtime(
    const char* phase, uint32_t original_sp, unsigned long long cycles) {
    std::fprintf(stderr,
        "[sma2-jit-push] %s pc=%08X mode=%s cycles=%llu r13=%08X r14=%08X "
        "cpsr=%08X r4-r7=%08X,%08X,%08X,%08X stack=",
        phase, (unsigned)g_cpu.R[15],
        (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm", cycles,
        (unsigned)g_cpu.R[13], (unsigned)g_cpu.R[14], (unsigned)g_cpu.cpsr,
        (unsigned)g_cpu.R[4], (unsigned)g_cpu.R[5], (unsigned)g_cpu.R[6],
        (unsigned)g_cpu.R[7]);
    if (original_sp >= 20u && !(original_sp & 3u)) {
        const uint32_t first = original_sp - 20u;
        for (unsigned i = 0; i < 5; ++i) {
            uint32_t word = 0;
            if (!sma2_debug_read_ram32(first + 4u * i, &word)) {
                std::fprintf(stderr, "unavailable");
                break;
            }
            std::fprintf(stderr, "%s%08X:%08X", i ? "," : "",
                         (unsigned)(first + 4u * i), (unsigned)word);
        }
    } else {
        std::fprintf(stderr, "unavailable");
    }
    std::fprintf(stderr, "\n");
}

// ── ARMMemory thunks: every guest access goes to GBARecomp's bus ─────
// With SMA2_JIT_SLOWMEM the generated code reaches these for EVERY access, so
// the access cost is GBARecomp's own: waits_access_cycles plus mGBA's
// GBAMemoryStall, which runtime_wait_model.h:37 says is the same model the
// production interpreter charges. `cycleCounter` is what jit-emit.c:896
// _load32 / jit-emit.c:1023 _store32 add to cpu->cycles.
static int sma2_charge_access(struct ARMCore* c, uint32_t addr, uint32_t width,
                              int* cycle_counter) {
    const uint32_t pc = armcore_guest_pc();
    const int thumb = (c->cpsr.packed & CPSR_T_BIT) ? 1 : 0;
    uint32_t wait = runtime_mem_cycles(addr, width, 0u);
    wait += runtime_prefetch_stall_delta(wait, pc, (uint32_t)thumb);
    if (cycle_counter) *cycle_counter += (int)wait;
    return (int)wait;
}

// ARM7TDMI unaligned access behaviour, as mGBA's memory stubs do it: the
// access is aligned down and the result rotated.
static uint32_t sma2_load32(struct ARMCore* c, uint32_t addr, int* cc) {
    const uint32_t v = bus_read_u32(addr & 0xFFFFFFFCu);
    sma2_charge_access(c, addr, 4u, cc);
    const unsigned rot = (addr & 3u) * 8u;
    return rot ? ((v >> rot) | (v << (32u - rot))) : v;
}
static uint32_t sma2_load16(struct ARMCore* c, uint32_t addr, int* cc) {
    const uint32_t a = addr & 0xFFFFFFFEu;
    const uint32_t v = bus_read_u16(a);
    sma2_charge_access(c, a, 2u, cc);
    return (addr & 2u) ? ((v >> 16) | (v << 16)) : v;
}
static uint32_t sma2_load8(struct ARMCore* c, uint32_t addr, int* cc) {
    const uint32_t a = addr & 0xFFFFFFFCu;
    const uint32_t v = (uint32_t)bus_read_u8(a) | ((uint32_t)bus_read_u8(a + 1) << 8) |
                       ((uint32_t)bus_read_u8(a + 2) << 16) |
                       ((uint32_t)bus_read_u8(a + 3) << 24);
    sma2_charge_access(c, addr, 1u, cc);
    return v >> ((addr & 3u) * 8u);
}
static void sma2_store32(struct ARMCore* c, uint32_t addr, int32_t v, int* cc) {
    const uint32_t a = addr & 0xFFFFFFFCu;
    const uint32_t value = (uint32_t)v;
    const unsigned rot = (addr & 3u) * 8u;
    const uint32_t rotated = rot ? ((value << rot) | (value >> (32u - rot))) : value;
    bus_write_u32(a, rotated);
    sma2_charge_access(c, a, 4u, cc);
    runtime_resync_horizon();
}
static void sma2_store16(struct ARMCore* c, uint32_t addr, int16_t v, int* cc) {
    const uint32_t a = addr & 0xFFFFFFFEu;
    bus_write_u16(a, (uint16_t)v);
    sma2_charge_access(c, a, 2u, cc);
    runtime_resync_horizon();
}
static void sma2_store8(struct ARMCore* c, uint32_t addr, int8_t v, int* cc) {
    bus_write_u8(addr, (uint8_t)v);
    sma2_charge_access(c, addr, 1u, cc);
    runtime_resync_horizon();
}

// jit.c:662 _arrive, arm.c:212 ARMStep and jit-emit.c:879 _setPC all reach
// memory through (activeRegion, activeMask). Those MUST be a real array plus
// an offset mask: _arrive does LOAD_32(prefetch[0], pc & activeMask,
// activeRegion) with no region dispatch. GBARecomp's bus decodes addr >> 24
// per access, so the "active region" only has to name the array the guest PC
// lives in.
static void sma2_set_active_region(struct ARMCore* c, uint32_t guest_pc) {
    gba::GbaBus* bus = gbarecomp::active_bus();
    const uint32_t region = (guest_pc >> 24) & 0xFu;
    const uint32_t* arr = nullptr;
    uint32_t mask = 0u;
    if (bus) {
        switch (region) {
            case 0x2:  // EWRAM
                arr = (const uint32_t*)bus->ewram_ptr();
                mask = 0x0003FFFFu;
                break;
            case 0x3:  // IWRAM
                arr = (const uint32_t*)bus->iwram_read_ptr();
                mask = 0x00007FFFu;
                break;
            default:  // cart regions
                arr = (const uint32_t*)bus->rom_ptr();
                mask = bus->rom_size() ? (uint32_t)(bus->rom_size() - 1u) : 0u;
                break;
        }
    }
    if (!arr) {  // never hand a null array to LOAD_32
        static uint32_t fallback;
        arr = &fallback;
        mask = 0u;
    }
    c->memory.activeRegion = arr;
    c->memory.activeMask = mask;
    c->memory.activeSeqCycles32 = g_runtime_waits.s32[region];
    c->memory.activeSeqCycles16 = g_runtime_waits.s16[region];
    c->memory.activeNonseqCycles32 = g_runtime_waits.n32[region];
    c->memory.activeNonseqCycles16 = g_runtime_waits.n16[region];
}

// jit-emit.c:1276 _emitStall is only reachable from the region fast paths,
// which SMA2_JIT_SLOWMEM removes, so this should never run. Keep it honest
// rather than wrong if it ever does.
static int32_t sma2_stall(struct ARMCore* c, int32_t wait) {
    const uint32_t pc = armcore_guest_pc();
    const int thumb = (c->cpsr.packed & CPSR_T_BIT) ? 1 : 0;
    return runtime_prefetch_stall_delta((uint32_t)wait, pc, (uint32_t)thumb);
}

static uint32_t sma2_load_multiple(struct ARMCore* c, uint32_t base, int mask,
                                   enum LSMDirection dir, int* cc) {
    const uint32_t count = (uint32_t)__builtin_popcount((unsigned)mask & 0xFFFFu);
    const bool down = (dir & LSM_D) != 0;
    const bool before = (dir & LSM_B) != 0;
    uint32_t addr = down ? base - count * 4u + (before ? 0u : 4u)
                         : base + (before ? 4u : 0u);
    for (int i = 0; i < 16; ++i) {
        if (mask & (1 << i)) {
            c->gprs[i] = (int32_t)bus_read_u32(addr & 0xFFFFFFFCu);
            sma2_charge_access(c, addr & 0xFFFFFFFCu, 4u, cc);
            addr += 4u;
        }
    }
    return down ? base - count * 4u : base + count * 4u;
}
static uint32_t sma2_store_multiple(struct ARMCore* c, uint32_t base, int mask,
                                    enum LSMDirection dir, int* cc) {
    const uint32_t count = (uint32_t)__builtin_popcount((unsigned)mask & 0xFFFFu);
    const bool down = (dir & LSM_D) != 0;
    const bool before = (dir & LSM_B) != 0;
    uint32_t addr = down ? base - count * 4u + (before ? 0u : 4u)
                         : base + (before ? 4u : 0u);
    for (int i = 0; i < 16; ++i) {
        if (mask & (1 << i)) {
            bus_write_u32(addr & 0xFFFFFFFCu, (uint32_t)c->gprs[i]);
            sma2_charge_access(c, addr & 0xFFFFFFFCu, 4u, cc);
            addr += 4u;
        }
    }
    runtime_resync_horizon();
    return down ? base - count * 4u : base + count * 4u;
}

// ── ARMInterruptHandler thunks ───────────────────────────────────────
// GBARecomp's scheduler owns events, so processEvents is a no-op here: the
// driver below calls runtime_tick + runtime_should_yield at block boundaries
// instead.
static void sma2_irq_reset(struct ARMCore*) {}
static void sma2_irq_process_events(struct ARMCore*) {}
static void sma2_irq_read_cpsr(struct ARMCore*) {}
static void sma2_irq_illegal(struct ARMCore*, uint32_t opcode) {
    // mGBA's JIT reports instructions it cannot compile through this hook.
    // Leave instruction semantics and timing to GBARecomp's interpreter.
    jit_illegal_seen = true;
    jit_illegal_pc = armcore_guest_pc();
    jit_illegal_opcode = opcode;
    std::fprintf(stderr,
                 "[sma2-jit] unsupported opcode %08X at guest pc %08X; "
                 "handing instruction to production interpreter\n",
                 (unsigned)jit_illegal_opcode, (unsigned)jit_illegal_pc);
}
static void sma2_irq_stub(struct ARMCore*, uint32_t opcode) {
    sma2_irq_illegal(0, opcode);
}
static void sma2_irq_bkpt(struct ARMCore*, int) {}

static void sma2_swi(struct ARMCore* c, uint32_t imm, int thumb) {
    // Hand the SWI to GBARecomp, which owns BIOS entry. The guest state is
    // GBARecomp's, so round-trip through g_cpu, let runtime_swi run the whole
    // BIOS call, then rebuild mGBA's pipeline view from the returned PC.
    armcore_store_into_g_cpu();
    g_cpu.R[15] = armcore_guest_pc() + (thumb ? 2u : 4u);
    runtime_swi(imm);
    armcore_load_from_g_cpu();
    armcore_set_pc_from_guest(g_cpu.R[15], (g_cpu.cpsr & CPSR_T_BIT) ? 1 : 0);
    sma2_set_active_region(&cpu, g_cpu.R[15]);
}
static void sma2_swi16(struct ARMCore* c, int immediate) {
    sma2_swi(c, (uint32_t)immediate, 1);
}
static void sma2_swi32(struct ARMCore* c, int immediate) {
    sma2_swi(c, (uint32_t)immediate, 0);
}

// ── Backend init ─────────────────────────────────────────────────────
// arm.c:51 ARMInit runs cpu->master->init(cpu, master); struct GBA starts with
// struct mCPUComponent d (cpu.h:36), so gba.d.init must be a real function or
// ARMInit calls through null.
static void sma2_gba_init(void* c, struct mCPUComponent* comp) {
    (void)c;
    (void)comp;
}
static void sma2_gba_deinit(struct mCPUComponent* comp) { (void)comp; }

bool init_backend() {
    if (backend_ready) return true;

    struct GBAMemory* m = &gba.memory;
    std::memset(m, 0, sizeof(*m));

    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) {
        std::fprintf(stderr, "[sma2-jit] no active GBARecomp bus at backend init\n");
        return false;
    }
    uint8_t* ew = bus->ewram_ptr();
    // The dynarec is handed the raw IWRAM array. Under SMA2_JIT_SLOWMEM the region fast paths are not emitted
    // (jit-emit.c:1496,1743) so guest stores are expected to reach GbaBus::write*, but that has NOT been verified
    // against the IWRAM code-watch barrier, so CMake rejects relocated IWRAM AOT + SMA2_SH4_DYNAREC outright.
    // iwram_write_ptr() invalidates the watch now, for any future caller.
    uint8_t* iw = bus->iwram_write_ptr("sma2_sh4_dynarec_raw_iwram");
    uint8_t* vr = bus->vram_ptr();
    uint8_t* om = bus->oam_ptr();
    uint8_t* pl = bus->pal_ptr();
    const uint8_t* rm = bus->rom_ptr();

    // jit.c:1856 ARMJITUpdateMemory hands these to generated code as uint32_t*.
    if ((((uintptr_t)ew | (uintptr_t)iw | (uintptr_t)vr | (uintptr_t)om |
         (uintptr_t)pl | (uintptr_t)rm) & 3u) != 0u) {
        std::fprintf(stderr,
                     "[sma2-jit] GBARecomp memory arrays are not 4-byte aligned; "
                     "the dynarec's word-pointer paths cannot alias them\n");
        backend_init_failed = true;
        return false;
    }
    m->wram = (uint32_t*)ew;
    m->iwram = (uint32_t*)iw;
    m->rom = (uint32_t*)rm;
    m->romSize = (uint32_t)bus->rom_size();
    // The JIT's cart fetch path uses an address mask, so only power-of-two ROM
    // images can be represented without aliasing the wrong bytes. Refuse JIT
    // initialization for other sizes; the runner then keeps using its
    // interpreter instead of silently executing a mis-masked cartridge.
    rom_mask_supported = m->romSize &&
                         (m->romSize & (m->romSize - 1u)) == 0u;
    if (!rom_mask_supported) {
        std::fprintf(stderr,
                     "[sma2-jit] ROM size %u is not a nonzero power of two; "
                     "dynarec disabled, interpreter remains active\n",
                     (unsigned)m->romSize);
        backend_init_failed = true;
        return false;
    }
    m->romMask = (uint32_t)(m->romSize ? m->romSize - 1u : 0u);
    m->fullBios = 1;  // GBARecomp always runs the real BIOS
    // jit.c:362 ARMJITSource refuses BIOS and RAM under SMA2_JIT_ROM_ONLY, so
    // memory->bios stays null: BIOS can never be translated.
    m->bios = nullptr;

    // jit.c:1856 ARMJITUpdateMemory bakes these into the stubs' cycle
    // constants and jit-emit.c:1856 ARMJITTimingKey flushes blocks when they
    // change. Mirrored from GBARecomp's live table.
    for (unsigned r = 0; r < 16u; ++r) {
        m->waitstatesSeq32[r] = (char)g_runtime_waits.s32[r];
        m->waitstatesSeq16[r] = (char)g_runtime_waits.s16[r];
        m->waitstatesNonseq32[r] = (char)g_runtime_waits.n32[r];
        m->waitstatesNonseq16[r] = (char)g_runtime_waits.n16[r];
    }
    m->prefetch = g_runtime_waits.prefetch != 0u;
    m->lastPrefetchedPc = g_runtime_waits.last_prefetched_pc;

    gba.haltPending = false;
    gba.performingDMA = 0;
    gba.idleOptimization = IDLE_LOOP_IGNORE;  // no idle-loop removal
    gba.cpu = &cpu;
    gba.bus = 0;

    cpu.master = (struct mCPUComponent*)&gba;
    gba.d.init = sma2_gba_init;
    gba.d.deinit = sma2_gba_deinit;

    cpu.memory.load32 = sma2_load32;
    cpu.memory.load16 = sma2_load16;
    cpu.memory.load8 = sma2_load8;
    cpu.memory.store32 = sma2_store32;
    cpu.memory.store16 = sma2_store16;
    cpu.memory.store8 = sma2_store8;
    cpu.memory.loadMultiple = sma2_load_multiple;
    cpu.memory.storeMultiple = sma2_store_multiple;
    cpu.memory.setActiveRegion = sma2_set_active_region;
    cpu.memory.stall = sma2_stall;

    cpu.irqh.reset = sma2_irq_reset;
    cpu.irqh.processEvents = sma2_irq_process_events;
    cpu.irqh.swi16 = sma2_swi16;
    cpu.irqh.swi32 = sma2_swi32;
    cpu.irqh.hitIllegal = sma2_irq_illegal;
    cpu.irqh.bkpt16 = sma2_irq_bkpt;
    cpu.irqh.bkpt32 = sma2_irq_bkpt;
    cpu.irqh.readCPSR = sma2_irq_read_cpsr;
    cpu.irqh.hitStub = sma2_irq_stub;

    ARMInit(&cpu);

    // jit.h:33 says to call this after the memory hooks are installed.
    if (!ARMJITInit(&cpu)) {
        std::fprintf(stderr, "[sma2-jit] ARMJITInit failed (code cache alloc)\n");
        backend_init_failed = true;
        return false;
    }

    backend_ready = true;
    return true;
}

// ── Region policy ────────────────────────────────────────────────────
// Only the cart regions are translated. BIOS (0x00) is GBARecomp's to own
// (its open-bus prefetch latch, runtime_bus_bridge.cpp:1013, and runtime_swi
// / runtime_irq drive the BIOS), and EWRAM/IWRAM (0x02/0x03) are mutable:
// GBARecomp's DMA writes reach them without going through jit.c's _store32
// wrappers, so jit.c's store-invalidation cannot see them and a translated
// RAM block could keep running stale instructions after a DMA code copy.
static bool jit_region_allowed(uint32_t guest_pc) {
    // GBARecomp constructs and activates its bus after parsing the ROM. The
    // runner selects the backend before that point, so defer mGBA/JIT setup
    // until this first per-step query, when the live bus is available.
    if (!backend_ready && !backend_init_failed && gbarecomp::active_bus()) {
        std::fprintf(stderr, "[sma2-jit] active bus found; initializing backend\n");
        const bool ready = init_backend();
        std::fprintf(stderr, "[sma2-jit] backend init %s\n",
                     ready ? "succeeded" : "failed; interpreter fallback active");
    }
    return rom_mask_supported && (guest_pc >> 24) >= 0x8u;
}

// ── The driver GBARecomp's runner calls ──────────────────────────────
// Runs translated blocks until the scheduler says to stop, or until control
// reaches a region the dynarec does not translate, then leaves g_cpu exactly
// between two guest instructions, as the interpreter would.
//
// ARMJITRunLoop (jit.c:783) is `while (cpu->cycles < cpu->nextEvent) ARMJITRun
// (cpu)`, and ARMJITRun falls back to mGBA's own ARMStep when it cannot
// compile a block. We call ARMJITRun ourselves so we can re-check the region
// between blocks and hand a non-translatable PC back to GBARecomp's
// interpreter instead of running it on mGBA's.
// jit.c:789 ARMJITRunLoop is `while (cpu->cycles < cpu->nextEvent) ARMJITRun
// (cpu)`. We call ARMJITRun (one block, jit.c:710) ourselves so we can
// re-check the region between blocks and hand a non-translatable PC back to
// GBARecomp's interpreter rather than to mGBA's. nextEvent starts as a distant
// absolute-cycle sentinel, but mGBA's _ARMSetMode deliberately changes it to
// cpu->cycles on an ARM/Thumb transition (isa-inlines.h:97-112). That makes the
// next block exit at its entry guard, giving the adapter a synchronization
// boundary to commit cycles and service GBARecomp's scheduler before retrying
// the same guest PC through the interpreter. The burst length is ours:
// SMA2_JIT_BURST_CYCLES guest cycles before we ask GBARecomp whether to yield.
#ifndef SMA2_JIT_BURST_CYCLES
#define SMA2_JIT_BURST_CYCLES 4096
#endif
int drive_dynarec(uint32_t* blocks_run, uint32_t* fallback_steps) {
    if (!backend_ready) return 0;

    static bool logged_first_drive = false;
    if (!logged_first_drive) {
        std::fprintf(stderr, "[sma2-jit] entering first driver burst pc=%08X\n",
                     (unsigned)g_cpu.R[15]);
        logged_first_drive = true;
    }

    // Each stage arms only from a committed live guest boundary, after the
    // prior comparison passed. Runtime SWI/trampoline behavior remains outside
    // each snapshot/replay interval.
    if (!diagnostic_finished) {
        if (first_div_active) {
            if (first_div_attempts >= kFirstDivMaxBlocks) {
                first_div_status = "INELIGIBLE";
                first_div_stop_reason = "four-block comparison limit reached";
                diagnostic_finished = true;
                return 0;
            }
            gba::GbaBus* bus = gbarecomp::active_bus();
            const uint32_t entry_pc = g_cpu.R[15];
            if (!bus || !arm_one_block_diff(first_div_diff, "firstdiv", entry_pc)) {
                first_div_status = "BLOCKED";
                first_div_stop_reason = "no active bus or fresh entry snapshot unavailable";
                g_stop_condition = first_div_stop_reason;
                diagnostic_finished = true;
                return 0;
            }
            const uint16_t entry_hw = bus->read16(entry_pc);
            const armv4t::Instr entry_insn =
                armv4t::ThumbDecoder::decode(entry_hw, entry_pc);
            if (entry_insn.op == armv4t::IrOp::SWI ||
                entry_insn.op == armv4t::IrOp::Undefined ||
                entry_insn.is_undefined) {
                first_div_diff.status = "INELIGIBLE";
                first_div_status = "INELIGIBLE";
                first_div_stop_reason = entry_insn.op == armv4t::IrOp::SWI
                    ? "next native boundary begins with SWI"
                    : "next native boundary begins with undefined opcode";
                g_stop_condition = first_div_stop_reason;
                diagnostic_finished = true;
                return 0;
            }
            first_div_single_block = true;
            std::fprintf(stderr,
                "[sma2-jit-diff] firstdiv block%u armed at committed entry "
                "pc=%08X/thumb hw=%04X sp=%08X lr=%08X cpsr=%08X cycles=%llu\n",
                first_div_attempts + 1, (unsigned)entry_pc, (unsigned)entry_hw,
                (unsigned)g_cpu.R[13], (unsigned)g_cpu.R[14],
                (unsigned)g_cpu.cpsr, (unsigned long long)g_runtime_cycles);
        } else if (!stage_a_completed) {
            stage_a_begin();
        } else if (g_cpu.R[15] == kStageBEntryPc && (g_cpu.cpsr & CPSR_T_BIT)) {
            if (arm_one_block_diff(stage_b, "stage2", kStageBEntryPc)) {
                stage_b_single_block = true;
                std::fprintf(stderr,
                    "[sma2-jit-diff] stage2 armed at trampoline return "
                    "(steps=%u block_runs=%u)\n",
                    (unsigned)diagnostic_steps, (unsigned)stage_b_block_runs);
            } else {
                g_stop_condition = "stage2 blocked: no active bus at the entry boundary";
                diagnostic_finished = true;
                return 0;
            }
        } else if (stage_b_completed && !stage_c_armed &&
                   g_cpu.R[15] == kStageCEntryPc &&
                   (g_cpu.cpsr & CPSR_T_BIT)) {
            if (!arm_one_block_diff(stage_c, "stage3", kStageCEntryPc)) {
                stage_c.status = "BLOCKED";
                g_stop_condition = "stage3 blocked: no active bus at committed entry";
                diagnostic_finished = true;
                return 0;
            }
            stage_c_armed = true;
            if (preflight_iwram_strh(stage_c, "stage3", kStageCStrhPc)) {
                stage_c_single_block = true;
                std::fprintf(stderr,
                    "[sma2-jit-diff] stage3 stage_c_entry_pc=%08X "
                    "stage_c_entry_mode=%s stage_c_entry_sp=%08X "
                    "stage_c_entry_lr=%08X stage_c_entry_cpsr=%08X "
                    "guest_cycles=%llu steps=%u block_runs=%u\n",
                    (unsigned)g_cpu.R[15],
                    (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm",
                    (unsigned)g_cpu.R[13], (unsigned)g_cpu.R[14],
                    (unsigned)g_cpu.cpsr,
                    (unsigned long long)g_runtime_cycles,
                    (unsigned)diagnostic_steps, (unsigned)stage_c_block_runs);
            } else {
                g_stop_condition = "stage3 STRH preflight was outside approved private IWRAM replay";
                diagnostic_finished = true;
                return 0;
            }
        } else if (stage_c_completed && !stage_d_armed &&
                   g_cpu.R[15] == kStageDEntryPc &&
                   (g_cpu.cpsr & CPSR_T_BIT)) {
            if (!arm_one_block_diff(stage_d, "stageD", kStageDEntryPc)) {
                stage_d.status = "BLOCKED";
                g_stop_condition = "stageD blocked: no active bus at committed entry";
                diagnostic_finished = true;
                return 0;
            }
            stage_d_armed = true;
            if (preflight_iwram_strh(stage_d, "stageD", kStageDStrhPc)) {
                stage_d_single_block = true;
                std::fprintf(stderr,
                    "[sma2-jit-diff] stageD armed at committed entry "
                    "pc=%08X mode=%s sp=%08X lr=%08X cpsr=%08X cycles=%llu "
                    "steps=%u block_runs=%u\n",
                    (unsigned)g_cpu.R[15],
                    (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm",
                    (unsigned)g_cpu.R[13], (unsigned)g_cpu.R[14],
                    (unsigned)g_cpu.cpsr,
                    (unsigned long long)g_runtime_cycles,
                    (unsigned)diagnostic_steps, (unsigned)stage_d_block_runs);
            } else {
                stage_d.status = "INELIGIBLE";
                g_stop_condition =
                    "stageD STRH preflight was outside approved private IWRAM replay";
                diagnostic_finished = true;
                return 0;
            }
        } else if (stage_d_completed && !stage_e_armed &&
                   g_cpu.R[15] == kStageEEntryPc &&
                   (g_cpu.cpsr & CPSR_T_BIT)) {
            if (!arm_one_block_diff(stage_e, "stageE", kStageEEntryPc)) {
                stage_e.status = "BLOCKED";
                g_stop_condition = "stageE blocked: no active bus at committed entry";
                diagnostic_finished = true;
                return 0;
            }
            stage_e_armed = true;
            if (preflight_iwram_store(stage_e, "stageE", kStageEEntryPc)) {
                stage_e_single_block = true;
            } else {
                g_stop_condition =
                    "stageE first word store is outside approved private IWRAM replay";
                diagnostic_finished = true;
                return 0;
            }
            std::fprintf(stderr,
                "[sma2-jit-diff] stageE armed at committed entry "
                "pc=%08X mode=%s sp=%08X lr=%08X cpsr=%08X cycles=%llu "
                "steps=%u block_runs=%u\n",
                (unsigned)g_cpu.R[15],
                (g_cpu.cpsr & CPSR_T_BIT) ? "thumb" : "arm",
                (unsigned)g_cpu.R[13], (unsigned)g_cpu.R[14],
                (unsigned)g_cpu.cpsr,
                (unsigned long long)g_runtime_cycles,
                (unsigned)diagnostic_steps, (unsigned)stage_e_block_runs);
        }
    }
    armcore_load_from_g_cpu();
    if (stage_a.active) stage_a.native_start_cycles = cpu.cycles;
    if (stage_b_single_block) stage_b.native_start_cycles = cpu.cycles;
    if (stage_c_single_block) stage_c.native_start_cycles = cpu.cycles;
    if (stage_d_single_block) stage_d.native_start_cycles = cpu.cycles;
    if (stage_e_single_block) stage_e.native_start_cycles = cpu.cycles;
    if (first_div_single_block)
        first_div_diff.native_start_cycles = cpu.cycles;
    if (first_div_single_block)
        first_div_event_budget = runtime_event_budget();
    armcore_set_pc_from_guest(g_cpu.R[15], (g_cpu.cpsr & CPSR_T_BIT) ? 1 : 0);
    sma2_set_active_region(&cpu, g_cpu.R[15]);

    struct ARMJITStats before;
    ARMJITGetStats(&cpu, &before);

    bool handed_off = false;
    bool no_progress = false;
    bool diff_boundary = false;
    bool diagnostic_stop = false;
    bool stage_b_boundary_break = false;
    bool stage_c_boundary_break = false;
    bool stage_d_boundary_break = false;
    bool stage_e_boundary_break = false;
    bool first_div_return_boundary_break = false;
    uint32_t diff_native_exit_pc = 0;
    for (;;) {
        cpu.nextEvent = 0x7FFFFFFF;
        long long horizon = runtime_event_budget();
        if (horizon <= 0 || horizon > SMA2_JIT_BURST_CYCLES)
            horizon = SMA2_JIT_BURST_CYCLES;
        const int32_t burst_end = cpu.cycles + (int32_t)horizon;

        while (cpu.cycles < burst_end) {
            const uint32_t gpc = armcore_guest_pc();
            if (!jit_region_allowed(gpc)) {
                handed_off = true;
                break;
            }
            const int32_t before_cycles = cpu.cycles;
            const int32_t before_pc = cpu.gprs[ARM_PC];
            const int32_t before_sp = cpu.gprs[ARM_SP];
            const uint32_t before_cpsr = (uint32_t)cpu.cpsr.packed;
            const uint32_t before_prefetch = cpu.prefetch[0];
            static bool push_trace_done = false;
            const bool trace_push = gpc == 0x0800044Cu && !push_trace_done;
            if (trace_push) {
                push_trace_done = true;
                sma2_trace_push_core("jit-before", &cpu,
                                     (uint32_t)cpu.gprs[ARM_SP]);
            }
            const int32_t before_next_event = cpu.nextEvent;
            static unsigned trace_runs = 0;
            const bool trace = trace_runs < 12;
            struct ARMJITStats run_before = {};
            if (trace || stage_b_single_block || stage_c_single_block ||
                stage_d_single_block || stage_e_single_block || stage_a.active ||
                first_div_single_block || first_div_waiting_post_e_swi ||
                (stage_b_completed && !stage_c_armed) ||
                (stage_c_completed && !stage_d_armed) ||
                (stage_d_completed && !stage_e_armed))
                ARMJITGetStats(&cpu, &run_before);
            if (trace) {
                std::fprintf(stderr,
                    "[sma2-jit] run[%u] enter pc=%08X arm_pc=%08X mode=%s cycles=%d\n",
                    trace_runs, (unsigned)gpc, (unsigned)before_pc,
                    cpu.executionMode == MODE_THUMB ? "thumb" : "arm",
                    (int)before_cycles);
            }
            // ARMJITRun's return value is the jitCount delta, and JIT_COUNT is
            // not defined for the Dreamcast target (jit.h:37 says it is 0 for a
            // block run there), so progress is measured from the cpu itself.
            jit_illegal_seen = false;
            ARMJITRun(&cpu);
            struct ARMJITStats run_after = {};
            if (trace || stage_b_single_block || stage_c_single_block ||
                stage_d_single_block || stage_e_single_block || stage_a.active ||
                first_div_single_block || first_div_waiting_post_e_swi ||
                (stage_b_completed && !stage_c_armed) ||
                (stage_c_completed && !stage_d_armed) ||
                (stage_d_completed && !stage_e_armed))
                ARMJITGetStats(&cpu, &run_after);
            if (first_div_waiting_post_e_swi) {
                ++first_div_transition_runs;
                if (first_div_transition_runs > kFirstDivMaxTransitionRuns) {
                    first_div_status = "NOT_REACHED";
                    first_div_stop_reason =
                        "post-E SWI return exceeded the native transition-run budget";
                    g_stop_condition = first_div_stop_reason;
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    break;
                }
                const uint32_t after_pc = armcore_guest_pc();
                const bool after_in_cart = after_pc >= 0x08000000u &&
                    after_pc < 0x0E000000u;
                const bool after_in_swi = after_pc >= 0x0809DFBCu &&
                    after_pc < 0x0809DFC0u;
                if (after_in_cart && !after_in_swi) {
                    armcore_store_into_g_cpu();
                    g_cpu.R[15] = after_pc;
                    first_div_return_boundary_break = true;
                    std::fprintf(stderr,
                        "[sma2-jit-diff] firstdiv committed post-SWI "
                        "boundary pc=%08X mode=%s transition_runs=%u\n",
                        (unsigned)after_pc,
                        (cpu.cpsr.packed & CPSR_T_BIT) ? "thumb" : "arm",
                        first_div_transition_runs);
                    break;
                }
            }
            if (stage_a.active) {
                stage_a.native_block_runs +=
                    (uint32_t)(run_after.blockRuns - run_before.blockRuns);
                stage_a.native_fallback_steps +=
                    (uint32_t)(run_after.fallbackSteps - run_before.fallbackSteps);
            }
            if (stage_a_completed && !stage_b_completed && !diagnostic_finished) {
                ++stage_b_block_runs;
                if (stage_b_block_runs > kStageBMaxBlockRuns) {
                    stage_b.status = "INELIGIBLE";
                    g_stop_condition =
                        "stage2 entry boundary not reached within the block-run budget";
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    break;
                }
            }
            if (stage_b_completed && !stage_c_armed &&
                !diagnostic_finished) {
                stage_c_block_runs +=
                    (unsigned)(run_after.blockRuns - run_before.blockRuns);
                if (stage_c_block_runs > kStageCMaxBlockRuns) {
                    stage_c.status = "NOT_REACHED";
                    g_stop_condition = "stage3 entry 080015FA not reached within finite block-run budget";
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    break;
                }
            }
            if (stage_c_completed && !stage_d_armed && !diagnostic_finished) {
                stage_d_block_runs +=
                    (unsigned)(run_after.blockRuns - run_before.blockRuns);
                if (stage_d_block_runs > kStageDMaxBlockRuns) {
                    stage_d.status = "NOT_REACHED";
                    g_stop_condition =
                        "stageD entry 0800160A not reached within bounded post-stage3 block budget";
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    break;
                }
            }
            if (stage_d_completed && !stage_e_armed && !diagnostic_finished) {
                stage_e_block_runs +=
                    (unsigned)(run_after.blockRuns - run_before.blockRuns);
                if (stage_e_block_runs > kStageEMaxBlockRuns) {
                    stage_e.status = "NOT_REACHED";
                    g_stop_condition =
                        "stageE entry 0800161A not reached within bounded post-D block budget";
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    break;
                }
            }
            // The trampoline can return inside a burst. End the burst at that
            // guest boundary so the next runner boundary is the isolated
            // stage-B entry; the snapshot is taken there, immediately before
            // the single native block run.
            if (stage_a_completed && !diagnostic_finished && !stage_b_single_block &&
                armcore_guest_pc() == kStageBEntryPc &&
                (cpu.cpsr.packed & CPSR_T_BIT)) {
                armcore_store_into_g_cpu();
                g_cpu.R[15] = armcore_guest_pc();
                std::fprintf(stderr,
                    "[sma2-jit-diff] stage2 boundary reached mid-burst pc=%08X cycles=%d "
                    "(steps=%u block_runs=%u)\n",
                    (unsigned)armcore_guest_pc(), (int)cpu.cycles,
                    (unsigned)diagnostic_steps, (unsigned)stage_b_block_runs);
                stage_b_boundary_break = true;
                break;
            }
            if (stage_b_completed && !diagnostic_finished && !stage_c_armed &&
                armcore_guest_pc() == kStageCEntryPc &&
                (cpu.cpsr.packed & CPSR_T_BIT)) {
                armcore_store_into_g_cpu();
                g_cpu.R[15] = armcore_guest_pc();
                stage_c_boundary_break = true;
                break;
            }
            if (stage_c_completed && !diagnostic_finished && !stage_d_armed &&
                armcore_guest_pc() == kStageDEntryPc &&
                (cpu.cpsr.packed & CPSR_T_BIT)) {
                armcore_store_into_g_cpu();
                g_cpu.R[15] = armcore_guest_pc();
                stage_d_boundary_break = true;
                break;
            }
            if (stage_d_completed && !diagnostic_finished && !stage_e_armed &&
                armcore_guest_pc() == kStageEEntryPc &&
                (cpu.cpsr.packed & CPSR_T_BIT)) {
                armcore_store_into_g_cpu();
                g_cpu.R[15] = armcore_guest_pc();
                stage_e_boundary_break = true;
                break;
            }
            if (trace_push) {
                sma2_trace_push_core("jit-after", &cpu, (uint32_t)before_sp);
                std::fprintf(stderr,
                    "[sma2-jit-push] jit-exit blocks=+%llu fallback=+%u "
                    "cycles=%d->%d nextEvent=%d->%d pc=%08X->%08X exits=",
                    (unsigned long long)(run_after.blockRuns - run_before.blockRuns),
                    (unsigned)(run_after.fallbackSteps - run_before.fallbackSteps),
                    (int)before_cycles, (int)cpu.cycles,
                    (int)before_next_event, (int)cpu.nextEvent,
                    (unsigned)armcore_guest_pc(), (unsigned)cpu.gprs[ARM_PC]);
                static const char* const push_exit_names[] = {
                    "event", "site_fall", "site_branch", "branch",
                    "sync", "lookup"
                };
                bool any_exit = false;
                for (unsigned i = 0;
                     i < sizeof(push_exit_names) / sizeof(push_exit_names[0]); ++i) {
                    const uint32_t delta = run_after.exits[i] - run_before.exits[i];
                    if (delta) {
                        std::fprintf(stderr, "%s:+%u,", push_exit_names[i],
                                     (unsigned)delta);
                        any_exit = true;
                    }
                }
                if (!any_exit) std::fprintf(stderr, "none");
                std::fprintf(stderr, "\n");
            }
            if (trace) {
                std::fprintf(stderr,
                    "[sma2-jit] run[%u] return pc=%08X arm_pc=%08X cycles=%d\n",
                    trace_runs, (unsigned)armcore_guest_pc(),
                    (unsigned)cpu.gprs[ARM_PC], (int)cpu.cycles);
            }
            ++trace_runs;
            if (!stage_a_completed && !stage_a.active &&
                armcore_guest_pc() == kStageAEntryPc) {
                // The requested entry can be reached mid-burst. Export the
                // just-returned block state before taking the shadow snapshot;
                // g_cpu still contains the previous runner boundary here.
                armcore_store_into_g_cpu();
                g_cpu.R[15] = armcore_guest_pc();
                stage_a_begin();
                if (stage_a.active) {
                    stage_a.native_start_cycles = cpu.cycles;
                    std::fprintf(stderr,
                        "[sma2-jit-diff] stage1 armed-after-block pc=%08X cycles=%d\n",
                        (unsigned)armcore_guest_pc(), (int)cpu.cycles);
                }
            }
            if (stage_b_single_block) {
                // Exactly one translated block is isolated for stage B.
                stage_b.native_block_runs =
                    (uint32_t)(run_after.blockRuns - run_before.blockRuns);
                stage_b.native_fallback_steps =
                    (uint32_t)(run_after.fallbackSteps - run_before.fallbackSteps);
                record_exit_reason(stage_b, run_before, run_after);
                stage_b.exit_pc = armcore_guest_pc();
                stage_b.native_exit_thumb = (cpu.cpsr.packed & CPSR_T_BIT) != 0;
                diff_boundary = true;
                diff_native_exit_pc = stage_b.exit_pc;
                break;
            }
            if (stage_c_single_block) {
                record_one_block_exit(stage_c, run_before, run_after);
                diff_boundary = true;
                diff_native_exit_pc = stage_c.exit_pc;
                break;
            }
            if (stage_d_single_block) {
                record_one_block_exit(stage_d, run_before, run_after);
                diff_boundary = true;
                diff_native_exit_pc = stage_d.exit_pc;
                break;
            }
            if (stage_e_single_block) {
                record_one_block_exit(stage_e, run_before, run_after);
                diff_boundary = true;
                diff_native_exit_pc = stage_e.exit_pc;
                break;
            }
            if (first_div_single_block) {
                record_one_block_exit(first_div_diff, run_before, run_after);
                diff_boundary = true;
                diff_native_exit_pc = first_div_diff.exit_pc;
                break;
            }
            if (stage_a.active) {
                const uint32_t after_pc = armcore_guest_pc();
                if (after_pc == kStageAExitPc ||
                    (after_pc > kStageAExitPc && after_pc < 0x0E000000u)) {
                    record_exit_reason(stage_a, run_before, run_after);
                    stage_a.exit_pc = after_pc;
                    stage_a.native_exit_thumb = (cpu.cpsr.packed & CPSR_T_BIT) != 0;
                    diff_boundary = true;
                    diff_native_exit_pc = after_pc;
                    std::fprintf(stderr,
                        "[sma2-jit-diff] stage1 native-return exit=%08X reasons=%s\n",
                        (unsigned)after_pc, stage_a.native_exit_detail);
                    break;
                }
            }
            if (jit_illegal_seen) {
                if (stage_c_single_block || stage_d_single_block ||
                    stage_e_single_block || first_div_single_block) {
                    IntervalDiff& active_diff = first_div_single_block
                        ? first_div_diff : stage_e_single_block ? stage_e
                        : stage_d_single_block ? stage_d : stage_c;
                    record_one_block_exit(active_diff, run_before, run_after);
                    active_diff.status = "INELIGIBLE";
                    g_stop_condition =
                        "one-block stage hit an unsupported opcode before native progress";
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    diff_boundary = true;
                    diff_native_exit_pc = active_diff.exit_pc;
                    break;
                }
                // _sysIllegal adds its nominal cost after calling the hook.
                // Undo it so the interpreter owns this instruction's timing.
                const int32_t illegal_cost = 1 +
                    (cpu.executionMode == MODE_THUMB
                         ? cpu.memory.activeSeqCycles16
                         : cpu.memory.activeSeqCycles32);
                cpu.cycles -= illegal_cost;
                if (armcore_guest_pc() != jit_illegal_pc) {
                    armcore_set_pc_from_guest(jit_illegal_pc,
                        (cpu.cpsr.packed & CPSR_T_BIT) ? 1 : 0);
                }
                no_progress = true;
                break;
            }
            if (cpu.cycles == before_cycles && cpu.gprs[ARM_PC] == before_pc) {
                if (stage_c_single_block || stage_d_single_block ||
                    stage_e_single_block || first_div_single_block) {
                    IntervalDiff& active_diff = first_div_single_block
                        ? first_div_diff : stage_e_single_block ? stage_e
                        : stage_d_single_block ? stage_d : stage_c;
                    record_one_block_exit(active_diff, run_before, run_after);
                    active_diff.status = "INELIGIBLE";
                    g_stop_condition = jit_illegal_seen
                        ? "one-block stage hit an unsupported opcode without native progress"
                        : "one-block stage native event/fallthrough exit made zero progress";
                    diagnostic_finished = true;
                    diagnostic_stop = true;
                    diff_boundary = true;
                    diff_native_exit_pc = active_diff.exit_pc;
                    break;
                }
                // No progress: preserve the exact instruction boundary and
                // hand this step to GBARecomp's production interpreter. Keep
                // the diagnostic bounded, but distinguish JIT execution from
                // the ARM interpreter fallback and show each JIT exit reason.
                static unsigned no_progress_reports = 0;
                if (trace && no_progress_reports < 4) {
                    static const char* const exit_names[] = {
                        "event", "site_fall", "site_branch", "branch",
                        "sync", "lookup"
                    };
                    std::fprintf(stderr,
                        "[sma2-jit] no-progress pc=%08X mode=%s blocks=+%llu fallback=+%u "
                        "cycles=%d->%d pc_reg=%08X->%08X sp=%08X->%08X "
                        "cpsr=%08X->%08X prefetch=%08X->%08X exits=",
                        (unsigned)gpc,
                        cpu.executionMode == MODE_THUMB ? "thumb" : "arm",
                        (unsigned long long)(run_after.blockRuns - run_before.blockRuns),
                        (unsigned)(run_after.fallbackSteps - run_before.fallbackSteps),
                        (int)before_cycles, (int)cpu.cycles,
                        (unsigned)before_pc, (unsigned)cpu.gprs[ARM_PC],
                        (unsigned)before_sp, (unsigned)cpu.gprs[ARM_SP],
                        (unsigned)before_cpsr, (unsigned)cpu.cpsr.packed,
                        (unsigned)before_prefetch, (unsigned)cpu.prefetch[0]);
                    bool any_exit = false;
                    for (unsigned i = 0; i < sizeof(exit_names) / sizeof(exit_names[0]); ++i) {
                        const uint32_t delta = run_after.exits[i] - run_before.exits[i];
                        if (delta) {
                            std::fprintf(stderr, "%s:+%u,", exit_names[i],
                                         (unsigned)delta);
                            any_exit = true;
                        }
                    }
                    if (!any_exit) std::fprintf(stderr, "none");
                    std::fprintf(stderr, "\n");
                    ++no_progress_reports;
                }
                no_progress = true;
                break;
            }
        }

        const uint32_t charged = (uint32_t)cpu.cycles;
        if (stage_a.active) {
            stage_a.native_cycles +=
                charged - (uint32_t)stage_a.native_start_cycles;
            stage_a.native_start_cycles = 0;
        }
        if (stage_b_single_block) {
            stage_b.native_cycles +=
                charged - (uint32_t)stage_b.native_start_cycles;
            stage_b.native_start_cycles = 0;
        }
        if (stage_c_single_block) {
            stage_c.native_cycles +=
                charged - (uint32_t)stage_c.native_start_cycles;
            stage_c.native_start_cycles = 0;
        }
        if (stage_d_single_block) {
            stage_d.native_cycles +=
                charged - (uint32_t)stage_d.native_start_cycles;
            stage_d.native_start_cycles = 0;
        }
        if (stage_e_single_block) {
            stage_e.native_cycles +=
                charged - (uint32_t)stage_e.native_start_cycles;
            stage_e.native_start_cycles = 0;
        }
        if (first_div_single_block) {
            first_div_diff.native_cycles +=
                charged - (uint32_t)first_div_diff.native_start_cycles;
            first_div_diff.native_start_cycles = 0;
        }
        // Commit the translated CPU state before runtime_tick: it can deliver
        // an IRQ by mutating g_cpu. Reloading before this commit used to restore
        // the previous burst's PC/registers and repeat guest code indefinitely.
        armcore_store_into_g_cpu();
        g_cpu.R[15] = armcore_guest_pc();
        cpu.cycles = 0;
        if (diff_boundary) {
            if (stage_b_single_block) {
                std::fprintf(stderr,
                    "[sma2-jit-diff] stage2 native exit=%08X pc=%08X mode=%s reason=%s "
                    "block_runs=%u fallback=%u cycles=%u\n",
                    (unsigned)diff_native_exit_pc, (unsigned)g_cpu.R[15],
                    stage_b.native_exit_thumb ? "thumb" : "arm",
                    stage_b.native_exit_reason,
                    (unsigned)stage_b.native_block_runs,
                    (unsigned)stage_b.native_fallback_steps,
                    (unsigned)stage_b.native_cycles);
                if (jit_illegal_seen) {
                    stage_b.status = "INELIGIBLE";
                    g_stop_condition = "stage2 block hit an unsupported opcode";
                } else if (stage_b.native_block_runs != 1 ||
                           stage_b.native_fallback_steps != 0) {
                    stage_b.status = "INELIGIBLE";
                    g_stop_condition =
                        "stage2 did not execute exactly one native block without fallback";
                } else if (stage_b.native_cycles == 0) {
                    stage_b.status = "INELIGIBLE";
                    g_stop_condition = "stage2 block charged no guest cycles";
                } else {
                    g_stop_condition = "stage2 comparison complete";
                }
                stage_b_single_block = false;
                stage_b_finish();
            } else if (stage_c_single_block) {
                std::fprintf(stderr,
                    "[sma2-jit-diff] stage3 native entry=%08X/%s exit=%08X/%s "
                    "reason=%s block_runs=%u fallback=%u cycles=%u unsupported=%s\n",
                    (unsigned)stage_c.entry_pc,
                    stage_c.snapshot.cpsr.t ? "thumb" : "arm",
                    (unsigned)diff_native_exit_pc,
                    stage_c.native_exit_thumb ? "thumb" : "arm",
                    stage_c.native_exit_reason,
                    (unsigned)stage_c.native_block_runs,
                    (unsigned)stage_c.native_fallback_steps,
                    (unsigned)stage_c.native_cycles,
                    jit_illegal_seen ? "yes" : "no");
                (void)one_block_eligible(stage_c, "stage3", true);
                stage_c_single_block = false;
                stage_c_finish();
            } else if (stage_d_single_block) {
                (void)one_block_eligible(stage_d, "stageD", true);
                stage_d_single_block = false;
                stage_d_finish();
            } else if (stage_e_single_block) {
                (void)one_block_eligible(stage_e, "stageE", true);
                stage_e_single_block = false;
                stage_e_finish();
            } else if (first_div_single_block) {
                if (first_div_event_budget <= 0 ||
                    (long long)first_div_diff.native_cycles >= first_div_event_budget) {
                    first_div_diff.status = "INELIGIBLE";
                    first_div_stop_reason =
                        "native block reached or crossed the next scheduler event boundary";
                    g_stop_condition = first_div_stop_reason;
                } else if (first_div_diff.native_exit_reason_count != 1) {
                    first_div_diff.status = "INELIGIBLE";
                    first_div_stop_reason =
                        "native block reported zero or multiple symbolic exit reasons";
                    g_stop_condition = first_div_stop_reason;
                } else {
                    (void)one_block_eligible(first_div_diff, "firstdiv", true);
                }
                first_div_finish_block();
            } else {
                std::fprintf(stderr,
                    "[sma2-jit-diff] stage1 native exit=%08X pc=%08X cycles=%u reason=%s\n",
                    (unsigned)diff_native_exit_pc, (unsigned)g_cpu.R[15],
                    (unsigned)stage_a.native_cycles, stage_a.native_exit_reason);
                const bool pass =
                    diff_compare(stage_a, "stage1", stage_a.native_cycles, false);
                stage_a.active = false;
                stage_a_completed = true;
                if (pass) {
                    g_stop_condition =
                        "stage1 passed; runner resumed for the single planned advancement";
                    std::fprintf(stderr,
                        "[sma2-jit-diff] stage1 PASS: outer runner may resume for stage2\n");
                } else {
                    g_stop_condition = "stage1 comparison failed; stopping immediately";
                    diagnostic_finished = true;
                }
            }
        }
        if (charged) runtime_tick(charged);
        // runtime_tick can vector an IRQ through the BIOS and move g_cpu.
        armcore_load_from_g_cpu();
        armcore_set_pc_from_guest(g_cpu.R[15], (g_cpu.cpsr & CPSR_T_BIT) ? 1 : 0);
        sma2_set_active_region(&cpu, g_cpu.R[15]);
        if (handed_off || no_progress || diff_boundary || diagnostic_stop ||
            stage_b_boundary_break || stage_c_boundary_break ||
            stage_d_boundary_break || stage_e_boundary_break ||
            first_div_return_boundary_break ||
            runtime_should_yield()) break;
    }

    armcore_store_into_g_cpu();
    g_cpu.R[15] = armcore_guest_pc();

    struct ARMJITStats after;
    ARMJITGetStats(&cpu, &after);
    if (blocks_run) *blocks_run = after.blockRuns - before.blockRuns;
    if (fallback_steps) *fallback_steps = after.fallbackSteps - before.fallbackSteps;
    // 1 = the dynarec executed something; 0 = it handed the whole step over to
    // GBARecomp's interpreter (non-translatable PC, or no progress).
    if (handed_off || no_progress) return 0;
    return after.blockRuns != before.blockRuns || after.fallbackSteps != before.fallbackSteps
               ? 1 : 0;
}

// ── Telemetry ────────────────────────────────────────────────────────
// jit.h:41 exposes the JIT's own counters; report them rather than infer.
void jit_snapshot(uint64_t* blocks_compiled, uint64_t* guest_insns_compiled,
                  uint64_t* code_bytes, uint64_t* block_runs,
                  uint64_t* fallback_steps, uint64_t* flushes,
                  uint64_t* invalidations, uint64_t* exits_total) {
    if (!backend_ready) {
        if (blocks_compiled) *blocks_compiled = 0;
        if (guest_insns_compiled) *guest_insns_compiled = 0;
        if (code_bytes) *code_bytes = 0;
        if (block_runs) *block_runs = 0;
        if (fallback_steps) *fallback_steps = 0;
        if (flushes) *flushes = 0;
        if (invalidations) *invalidations = 0;
        if (exits_total) *exits_total = 0;
        return;
    }
    struct ARMJITStats s;
    ARMJITGetStats(&cpu, &s);
    if (blocks_compiled) *blocks_compiled = s.blocksCompiled;
    if (guest_insns_compiled) *guest_insns_compiled = s.guestInsnsCompiled;
    if (code_bytes) *code_bytes = s.codeBytes;
    if (block_runs) *block_runs = s.blockRuns;
    if (fallback_steps) *fallback_steps = s.fallbackSteps;
    if (flushes) *flushes = s.flushes;
    if (invalidations) *invalidations = s.invalidations;
    if (exits_total) {
        uint64_t sum = 0;
        for (unsigned i = 0; i < sizeof(s.exits) / sizeof(s.exits[0]); ++i) sum += s.exits[i];
        *exits_total = sum;
    }
}

}  // namespace sma2

// C entry points the GBARecomp runner calls (see the overlay patch in
// tools/prepare_dreamcast_framework.py).
extern "C" int sma2_dc_jit_init(void) { return sma2::init_backend() ? 1 : 0; }
extern "C" int sma2_dc_jit_drive(uint32_t* blocks_run, uint32_t* fallback_steps) {
    return sma2::drive_dynarec(blocks_run, fallback_steps);
}
extern "C" uint32_t sma2_dc_jit_region_allowed(uint32_t guest_pc) {
    return sma2::jit_region_allowed(guest_pc) ? 1u : 0u;
}
extern "C" uint32_t sma2_dc_jit_diagnostic_complete(void) {
    return sma2::diagnostic_finished ? 1u : 0u;
}
extern "C" uint32_t sma2_dc_jit_diagnostic_step(void) {
    return sma2::diagnostic_step() ? 1u : 0u;
}
extern "C" void sma2_dc_jit_print_final_summary(void) {
    sma2::print_final_summary();
}
extern "C" void sma2_dc_jit_snapshot(uint64_t* blocks_compiled, uint64_t* guest_insns_compiled,
                                     uint64_t* code_bytes, uint64_t* block_runs,
                                     uint64_t* fallback_steps, uint64_t* flushes,
                                     uint64_t* invalidations, uint64_t* exits_total) {
    sma2::jit_snapshot(blocks_compiled, guest_insns_compiled, code_bytes, block_runs,
                       fallback_steps, flushes, invalidations, exits_total);
}
