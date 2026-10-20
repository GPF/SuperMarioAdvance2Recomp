// Scratch-only event tracer for the SMA2 native-vs-interp replay (NOT in repo).
// Includes the existing harness TU (renaming its main) so every helper is reused,
// and adds linker --wrap shims that log runtime events without changing state.
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <algorithm>
#include <array>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <fstream>
#include <iostream>
#include <optional>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <iterator>
#include <cerrno>
#include <deque>
#include <set>
#include <list>
#include <utility>
#include <limits>
#define private public
#define main sma2_harness_main
#include "/home/gpf/code/SuperMarioAdvance2Recomp/tools/sma2_snapshot_diff.cpp"
#include "heal_gate.h"
#include "/home/gpf/code/SuperMarioAdvance2Recomp/src/dreamcast_native_pvr_bridge_contract.h"
static bool g_cls = false;
struct ClsObs : gba::GbaPpu::PresentationObserver {
    uint8_t reason[160]{}; unsigned seen = 0; uint64_t frames = 0; std::map<uint16_t,unsigned> dc;
    void scanline(const gba::GbaPpu&, uint32_t y, uint16_t dispcnt, const uint8_t* io, const uint8_t*, const uint8_t* oam, const uint8_t*) override {
        if (y >= 160) return; reason[y] = gbarecomp::sma2_pvr_bridge::classify_fallback(dispcnt, io, oam); ++seen; ++dc[dispcnt]; }
    void frame_ready() override {
        unsigned e = 0, w = 0, b = 0, bm = 0, ms = 0;
        for (unsigned y = 0; y < 160; ++y) { e += reason[y] == 0; w += (reason[y] & 1) != 0; bm += (reason[y] & 2) != 0; b += (reason[y] & 4) != 0; ms += (reason[y] & 0x18) != 0; }
        std::printf("PVRCLS frame=%llu rows_seen=%u eligible=%u win=%u bitmap=%u blend=%u mosaic=%u dispcnt:", (unsigned long long)frames++, seen, e, w, bm, b, ms);
        for (auto& kv : dc) std::printf(" %04x x%u", kv.first, kv.second);
        std::printf("\n"); seen = 0; dc.clear(); }
    void restored() noexcept override {}
};
static ClsObs g_clsobs;
#include "iwram_code_guard.h"
#include "sma2_iwram_aot.h"
#undef main
#undef private

#include <algorithm>

// ───────── IWRAM AOT PoC guard (scratch-only; NOT production) ─────────
// Native entry into the relocated IWRAM corpus is permitted only while the live
// IWRAM bytes [kGuardBase, kGuardBase+kGuardLen) equal the frozen ROM image. The
// predicate is installed as g_runtime_force_interp_hook, which the production
// runtime already evaluates (a) in dispatch_resolve() on every runtime_dispatch,
// (b) in runtime_should_yield() at every generated per-instruction prologue
// (so direct C calls/gotos inside the block and mid-episode code writes are
// covered), and (c) before each IRQ-handler instruction. Nothing is cached: every
// call re-compares the live bytes, so no positive validation can go stale.
static const uint32_t kGuardBase = 0x03001518u, kGuardLen = 0x360u;
static std::vector<uint8_t> g_frozen;
struct CodeWatch : gba::BusWriteObserver {
    bool valid = false;
    uint64_t seen = 0, iw_seen = 0, overlap_seen = 0, noop_overlap = 0, invalidations = 0, gen = 0;
    uint64_t hook_calls = 0, fullcmp = 0, revalidations = 0, rejects = 0, hook_installs = 0;
    uint64_t fail_gen = ~0ull, cached_rejects = 0;
    uint64_t invariant_checks = 0, invariant_violations = 0; uint32_t first_violation_retired = 0;
    std::map<std::string, uint64_t> by_src;
    std::vector<std::string> log;
    bool on_bus_write(gba::BusWriteRegion r, uint32_t off, uint32_t addr, uint8_t w,
                      uint32_t oldv, uint32_t newv) override;
};
static CodeWatch g_cw;
struct ReplayEv { uint64_t frame; uint16_t key; }; static std::vector<ReplayEv> g_replay; static size_t g_replay_i = 0;   // --input-replay FILE (same semantics as GBARECOMP_INPUT_REPLAY: last event with frame <= ppu.frame_count())
static void apply_replay(gba::GbaBus& bus, uint64_t frame) { if (g_replay.empty()) return; while (g_replay_i + 1 < g_replay.size() && g_replay[g_replay_i + 1].frame <= frame) ++g_replay_i; if (g_replay[g_replay_i].frame <= frame) { if (bus.io().host_keyinput() != g_replay[g_replay_i].key) std::printf("REPLAY_KEY frame=%llu key=%04x\n", (unsigned long long)frame, g_replay[g_replay_i].key); bus.io().set_keyinput(g_replay[g_replay_i].key); } }
static uint64_t g_capture_every = 0; static std::string g_capture_every_prefix;   // --capture-every N:PREFIX -> PREFIX_<absolute ppu frame>.gbas
static std::string g_dump_ppm;   // --dump-ppm FILE: render the restored machine's live frame
static std::string g_audio_log;   // --audio-log FILE: per-vblank sound-register signature (music-change detection)
static std::vector<uint64_t> g_capture_frames; static std::string g_capture_frames_prefix;   // --capture-frames F1,F2,..:PREFIX (absolute ppu frames)
static uint64_t g_capture_at = 0; static std::string g_capture_file;   // --capture-at V:FILE : write a checkpoint image after V yields (restore-fidelity test)
static bool g_island_keep = false;  // --island-keep: leave the island flag exactly as sma2::iwram_aot_install decided it (tests the Arm-C media switch)
static bool g_island_off = false;  // --island-off (Arm-C model): force-interp runner + guard/barrier installed + generated code linked, native island DISABLED
static bool g_island = false;      // --island: native run = force-interp runner + native dispatch only at the static IWRAM entries (the DC normal-gameplay path)
static bool g_lib_guard = false;   // --lib-guard: library module iwram_code_guard (barrier + hook + post-restore reinstall) instead of the harness hook
static bool g_bus_watch = false;   // --bus-watch: use the in-bus IWRAM barrier (scratch GBARecomp) instead of the BusWriteObserver
static int g_in_swi = 0;
static int g_guard_on = 0;      // 0 off, 1 full, 2 entry-only, 3 event-driven invalidation (validates only at translated function entry PCs)
struct RPoke { uint64_t retired; uint32_t addr; uint8_t val; bool done; };
static std::vector<RPoke> g_rpokes;
static uint8_t* g_poke_iwram = nullptr;
static void apply_rpokes(uint64_t retired) {
    for (auto& p : g_rpokes) if (!p.done && retired >= p.retired) {
        std::printf("RPOKE retired=%llu (target %llu) addr=%08x old=%02x new=%02x\n", (unsigned long long)retired, (unsigned long long)p.retired, p.addr, g_poke_iwram[p.addr - 0x03000000u], p.val);
        g_poke_iwram[p.addr - 0x03000000u] = p.val; p.done = true; }
}
static const uint8_t* g_guard_iwram = nullptr;
struct GuardStats {
    uint64_t calls = 0, in_range = 0, pass = 0, fail = 0, fail_to_pass = 0, pass_to_fail = 0;
    int last = -1;
    std::vector<std::string> log;
} g_gs;
extern "C" __attribute__((noinline)) void iw_dbg_break(void) { asm volatile(""); }
extern "C" int iw_guard_hook(uint32_t pc, int thumb) {
    if (pc == 0x030015d8u) iw_dbg_break();
    ++g_gs.calls;
    if (!g_rpokes.empty()) apply_rpokes(runtime_fp_count());
    if (g_guard_on == 0) return 0;     // poke-only hook (unguarded control)
    if (g_guard_on == 2) {             // entry-only variant: validates only at translated entry PCs
        static const uint32_t ent[] = {0x03001518u,0x03001528u,0x030015D8u,0x030015F4u,0x030016B8u,0x030016F8u,0x03001708u};
        bool is_ent = false; for (uint32_t e : ent) is_ent |= (pc == e);
        if (!is_ent) return 0;
    }
    if (thumb || pc < kGuardBase || pc >= kGuardBase + kGuardLen) return 0;
    ++g_gs.in_range;
    const bool ok = std::memcmp(g_guard_iwram + (kGuardBase - 0x03000000u), g_frozen.data(), kGuardLen) == 0;
    if (ok) ++g_gs.pass; else ++g_gs.fail;
    if (g_gs.last != (ok ? 1 : 0)) {
        char b[160];
        std::snprintf(b, sizeof b, "GUARD_TRANSITION %s->%s at pc=%08x vbl=%llu retired=%u",
                      g_gs.last < 0 ? "init" : (g_gs.last ? "PASS" : "REJECT"), ok ? "PASS" : "REJECT", pc,
                      (unsigned long long)g_runtime_vblank_starts, runtime_fp_count());
        g_gs.log.push_back(b);
        if (g_gs.last == 0 && ok) ++g_gs.fail_to_pass;
        if (g_gs.last == 1 && !ok) ++g_gs.pass_to_fail;
        g_gs.last = ok ? 1 : 0;
    }
    return ok ? 0 : 1;
}
struct Poke { uint64_t vbl; uint32_t addr; uint8_t val; bool done = false; };
static std::vector<Poke> g_pokes;
static std::string g_ckpt2; static uint64_t g_vblanks2 = 2;
static bool g_skip_restore_invalidate = false; static bool g_final_snap = false;
static bool g_check_each_tick_env() { const char* e = std::getenv("IW_CHECK_EACH_TICK"); return e && e[0] == '1'; }
static void apply_pokes(Machine& m, uint64_t rel_vbl) {
    for (auto& pk : g_pokes) {
        if (pk.done || pk.vbl > rel_vbl) continue;
        uint8_t* iw = const_cast<uint8_t*>(m.bus.iwram_read_ptr());
        std::printf("POKE rel_vbl=%llu addr=%08x old=%02x new=%02x retired=%u\n", (unsigned long long)rel_vbl,
                    pk.addr, iw[pk.addr - 0x03000000u], pk.val, runtime_fp_count());
        iw[pk.addr - 0x03000000u] = pk.val; pk.done = true;
    }
}
static void print_guard_stats() {
    std::printf("GUARD_STATS enabled=%d hook_calls=%llu in_range_checks=%llu pass=%llu reject=%llu pass_to_reject=%llu reject_to_pass=%llu\n",
                g_guard_on, (unsigned long long)g_gs.calls, (unsigned long long)g_gs.in_range,
                (unsigned long long)g_gs.pass, (unsigned long long)g_gs.fail,
                (unsigned long long)g_gs.pass_to_fail, (unsigned long long)g_gs.fail_to_pass);
    for (auto& l : g_gs.log) std::printf("%s\n", l.c_str());
}

extern "C" {
extern unsigned long long g_runtime_vblank_starts;
extern unsigned long long g_runtime_cycles;
uint32_t runtime_call_stack_depth(void);
void __real_runtime_irq(uint32_t);
void __real_runtime_tick(uint32_t);
void __real_runtime_dispatch(uint32_t);
void __real_runtime_dispatch_miss(uint32_t);
void __real_runtime_exception_return(uint32_t);
void __real_runtime_idle_backedge(uint32_t);
void __real_runtime_swi(uint32_t);
void __real_runtime_insn_fp(void);
int runtime_has_static_entry(uint32_t pc, int thumb);
}

enum EvKind : uint16_t {
    EV_DISPATCH_IN = 1, EV_DISPATCH_OUT, EV_MISS_IN, EV_MISS_OUT, EV_IRQ_IN, EV_IRQ_OUT,
    EV_EXRET, EV_IDLE_IN, EV_IDLE_OUT, EV_YIELD, EV_TICK_VBL, EV_TICK_IRQ, EV_SWI_IN,
    EV_SWI_OUT, EV_STEP_IN, EV_STEP_OUT, EV_STOP_REQ, EV_OB
};
static const char* ev_name(uint16_t k) {
    static const char* n[] = {"?", "DISPATCH_IN", "DISPATCH_OUT", "MISS_IN", "MISS_OUT",
        "IRQ_IN", "IRQ_OUT", "EXRET", "IDLE_IN", "IDLE_OUT", "YIELD", "TICK_VBL",
        "TICK_IRQ", "SWI_IN", "SWI_OUT", "STEP_IN", "STEP_OUT", "STOP_REQ", "OB_CHANGE"};
    return n[k];
}
struct Ev {
    uint32_t seq; uint16_t kind; uint16_t nest;
    unsigned long long cyc;
    uint32_t a, pc, cpsr, depth;
    uint32_t vbl, yv;
};
static std::vector<Ev> g_ev;
static bool g_ev_on = false; static bool g_no_ev = false; static uint64_t g_step_guard = 5000000;   // --no-ev: skip per-step event log (long replays)
extern "C" unsigned long long g_runtime_irq_entries;
static uint32_t g_last_yielded = 0;
static void log_ev(uint16_t kind, uint32_t a) {
    if (!g_ev_on || g_no_ev) return;
    gbarecomp::RuntimeTimingContext tc{};
    gbarecomp::runtime_capture_timing_context(tc);
    g_ev.push_back({runtime_fp_count(), kind, (uint16_t)g_irq_nest_depth, g_runtime_cycles, a,
                    g_cpu.R[15], g_cpu.cpsr, runtime_call_stack_depth(),
                    (uint32_t)g_runtime_vblank_starts, (uint32_t)tc.yielded_vblank});
}
extern "C" void __wrap_runtime_irq(uint32_t ra) {
    log_ev(EV_IRQ_IN, ra); __real_runtime_irq(ra); log_ev(EV_IRQ_OUT, ra);
}
static gba::GbaBus* g_ob_bus = nullptr;
static uint32_t g_ob_last = 0;
static void cw_check_invariant();
static void tick_injects();
static bool g_check_each_tick_fwd();
extern "C" void __wrap_runtime_tick(uint32_t c) {
    if (g_check_each_tick_fwd()) cw_check_invariant();
    tick_injects();
    const unsigned long long v0 = g_runtime_vblank_starts, i0 = g_runtime_irq_entries;
    if (g_ob_bus && g_ev_on && g_ob_bus->open_bus_pc_ != g_ob_last) {
        g_ob_last = g_ob_bus->open_bus_pc_; log_ev(EV_OB, g_ob_last);
    }
    __real_runtime_tick(c);
    if (g_runtime_vblank_starts != v0) log_ev(EV_TICK_VBL, c);
    (void)i0;
}
static std::vector<uint8_t> g_ctx;      // 0 = native, 1 = bridge
static std::vector<uint8_t> g_tags;     // one per retired instruction (call order)
static bool g_tag_on = false;
// --digest: streaming per-instruction digest (cycles, pc, cpsr, R0-R15 = the fields of one fp record) + coverage counters,
// instead of storing every record (the framework fp ring holds only 2^23 records).
#include <map>
static int g_in_fis = 0; static std::map<uint32_t, uint64_t> g_interp_hist; static std::map<uint32_t, uint64_t> g_interp_out_hist; static bool g_ihist = false; struct IHE { uint64_t vbl, cyc; uint32_t pc, lr, cpsr; }; static std::vector<IHE> g_ihe;
struct WatchRg { uint32_t lo, hi; uint64_t last; bool seen; }; static std::vector<WatchRg> g_watch;   // --watch LO:HI (IWRAM offsets): print region hash when it changes
static bool g_digest = false; static uint64_t g_dg_h = 1469598103934665603ull, g_dg_n = 0, g_dg_mixer = 0, g_dg_iwram = 0, g_dg_native = 0, g_dg_native_mixer = 0;
extern "C" void __wrap_runtime_insn_fp(void) {
    if (g_digest) {
        uint64_t h = g_dg_h; auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(g_runtime_cycles); mix(g_cpu.R[15]); mix(g_cpu.cpsr); for (int i = 0; i < 16; ++i) mix(g_cpu.R[i]);
        g_dg_h = h; ++g_dg_n;
        const uint32_t pc = g_cpu.R[15];
        if (g_ihist && g_in_fis && pc >= 0x03001518u && pc < 0x03001878u) { ++g_interp_hist[pc]; if (g_ihe.size() < 64) g_ihe.push_back({g_runtime_vblank_starts, g_runtime_cycles, pc, g_cpu.R[14], g_cpu.cpsr}); }
        if (g_ihist && pc >= 0x03000000u && pc < 0x03008000u && !(pc >= 0x03001518u && pc < 0x03001878u)) ++g_interp_out_hist[pc | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u)];
        if (pc >= 0x03000000u && pc < 0x03008000u) { ++g_dg_iwram; if (pc >= 0x03001518u && pc < 0x03001878u) { ++g_dg_mixer; if (g_tag_on && !g_ctx.empty() && g_ctx.back() == 0) ++g_dg_native_mixer; } }
        if (g_tag_on && !g_ctx.empty() && g_ctx.back() == 0) ++g_dg_native;
        return;
    }
    if (g_tag_on) g_tags.push_back(g_ctx.empty() ? 0 : g_ctx.back());
    __real_runtime_insn_fp();
}
extern "C" void __wrap_runtime_dispatch(uint32_t t) {
    log_ev(EV_DISPATCH_IN, t);
    // Context tag: native only when a static entry exists AND the (side-effect-free) guard predicate
    // would not divert this dispatch to the interpreter bridge.
    const uint32_t tp = t & ~1u;
    const bool guard_rejects = g_guard_on && g_runtime_force_interp_hook && !(g_cpu.cpsr & CPSR_T_BIT) &&
        !(g_guard_on == 3 && g_cw.valid) &&
        tp >= kGuardBase && tp < kGuardBase + kGuardLen &&
        std::memcmp(g_guard_iwram + (kGuardBase - 0x03000000u), g_frozen.data(), kGuardLen) != 0;
    const bool st = !guard_rejects && runtime_has_static_entry(tp, (g_cpu.cpsr & CPSR_T_BIT) != 0);
    // guard-rejected dispatch runs the interpreter bridge directly (no dispatch_miss wrapper call): tag it bridged
    const int push = guard_rejects ? 1 : (st ? 0 : -1);
    if (push >= 0) g_ctx.push_back((uint8_t)push);
    __real_runtime_dispatch(t);
    if (push >= 0) g_ctx.pop_back();
    log_ev(EV_DISPATCH_OUT, t);
}
static uint64_t g_wrap_bridge_calls = 0;
extern "C" int __real_runtime_bridge_interpret(uint32_t, bool, uint32_t, uint64_t);
// Guard-forced (or hook-forced) interpretation enters the bridge directly from dispatch_resolve,
// not through runtime_dispatch_miss: tag those instructions "bridged" too.
extern "C" int __wrap_runtime_bridge_interpret(uint32_t pc, bool th, uint32_t a, uint64_t b) {
    ++g_wrap_bridge_calls; g_ctx.push_back(1);
    const int r = __real_runtime_bridge_interpret(pc, th, a, b);
    g_ctx.pop_back();
    return r;
}
static uint64_t g_wrap_fis_calls = 0;
extern "C" void __real_runtime_force_interp_step(void);
// IRQ-context interpretation (runtime_irq drive loop honouring the force-interp predicate) and the
// guard-rejected path both go through runtime_force_interp_step: tag as bridged/interpreted.
extern "C" void __wrap_runtime_force_interp_step(void) {
    if (!g_rpokes.empty()) apply_rpokes(runtime_fp_count());
    ++g_wrap_fis_calls; g_ctx.push_back(1); g_in_fis = 1; __real_runtime_force_interp_step(); g_in_fis = 0; g_ctx.pop_back();
}
extern "C" void __wrap_runtime_dispatch_miss(uint32_t t) {
    log_ev(EV_MISS_IN, t);
    g_ctx.push_back(1);
    __real_runtime_dispatch_miss(t);
    g_ctx.pop_back();
    log_ev(EV_MISS_OUT, t);
}
extern "C" void __wrap_runtime_exception_return(uint32_t t) {
    log_ev(EV_EXRET, t); __real_runtime_exception_return(t);
}
extern "C" void __wrap_runtime_idle_backedge(uint32_t t) {
    const unsigned long long c0 = g_runtime_cycles;
    __real_runtime_idle_backedge(t);
    if (g_runtime_cycles != c0) {
        log_ev(EV_IDLE_OUT, (uint32_t)(g_runtime_cycles - c0));
    }
}
extern "C" void __wrap_runtime_swi(uint32_t t) {
    ++g_in_swi; log_ev(EV_SWI_IN, t); __real_runtime_swi(t); log_ev(EV_SWI_OUT, t); --g_in_swi;
}
// Log every time the harness-level should-yield wrapper returns true. The harness
// already defines __wrap_runtime_should_yield; we observe via yielded counter change.


// ───────── Event-driven invalidation experiment (scratch; Phase 2/3 of the invalidation task) ─────────
// Model: `valid` == "live IWRAM code bytes are known equal to the frozen ROM image".
//  * Every GbaBus::write8/16/32 reaches the BusWriteObserver with the RESOLVED (mirror-folded) offset,
//    width, old and new value. A store whose byte range overlaps [0x1518,0x1878) AND changes at least
//    one overlapped byte invalidates (exact range; adjacent data and byte-identical rewrites do not).
//  * While invalid, g_runtime_force_interp_hook is installed. At an in-range PC it re-compares the full
//    image: mismatch => interpret live bytes (and unwind native frames at the next prologue);
//    match => valid again and the hook is REMOVED, so a valid image costs nothing per instruction.
//  * Bulk writers that bypass GbaBus::write* (checkpoint load, rollback, debug pointer writes) must call
//    cw_invalidate() explicitly — here the harness does for restore; ptrpoke is the red control.

static const char* g_label = nullptr;          // explicit source label (injectors)
static const char* cw_source_label();
static void cw_note(const char* what, uint32_t pc) {
    char b[200];
    std::snprintf(b, sizeof b, "%s pc=%08x retired=%u vbl=%llu gen=%llu", what, pc, runtime_fp_count(),
                  (unsigned long long)g_runtime_vblank_starts, (unsigned long long)g_cw.gen);
    g_cw.log.push_back(b);
}
extern "C" int iw_event_hook(uint32_t pc, int thumb);
static void cw_invalidate(const char* src) {
    ++g_cw.invalidations; ++g_cw.gen; ++g_cw.by_src[src];
    if (g_cw.valid || g_cw.log.empty() || g_cw.log.back().compare(0, 10, "INVALIDATE") != 0) {
        std::string w = std::string("INVALIDATE src=") + src; cw_note(w.c_str(), g_cpu.R[15]);
    }
    g_cw.valid = false;
    if (g_runtime_force_interp_hook != &iw_event_hook) { g_runtime_force_interp_hook = &iw_event_hook; ++g_cw.hook_installs; }
}
bool CodeWatch::on_bus_write(gba::BusWriteRegion r, uint32_t off, uint32_t, uint8_t w,
                             uint32_t oldv, uint32_t newv) {
    ++seen;
    if (r != gba::BusWriteRegion::Iwram) return false;
    ++iw_seen;
    const uint32_t lo = kGuardBase - 0x03000000u, hi = lo + kGuardLen;
    if (off + w <= lo || off >= hi) return false;
    ++overlap_seen;
    bool changed = false;
    for (uint32_t i = 0; i < w; ++i) {
        const uint32_t o = off + i;
        if (o >= lo && o < hi && ((oldv >> (8 * i)) & 0xFF) != ((newv >> (8 * i)) & 0xFF)) changed = true;
    }
    if (!changed) { ++noop_overlap; return false; }
    cw_invalidate(cw_source_label());
    return false;                                  // observe only: never suppress the store
}
static void lib_ev(const char* what, const char* detail, uint32_t pc) {
    if (!std::strcmp(what, "INVALIDATE")) {
        const char* src = !std::strcmp(detail, "bus_store") ? cw_source_label() : detail;
        ++g_cw.invalidations; ++g_cw.gen; ++g_cw.by_src[src];
        if (g_cw.valid || g_cw.log.empty() || g_cw.log.back().compare(0, 10, "INVALIDATE") != 0) {
            std::string w = std::string("INVALIDATE src=") + src; cw_note(w.c_str(), g_cpu.R[15]); }
        g_cw.valid = false;
    } else if (!std::strcmp(what, "REVALIDATE")) { g_cw.valid = true; ++g_cw.revalidations; cw_note("REVALIDATE(pass)", pc); }
    else if (!std::strcmp(what, "REJECT")) { ++g_cw.rejects; if (g_cw.log.empty() || g_cw.log.back().compare(0, 6, "REJECT") != 0) cw_note("REJECT(mismatch)", pc); }
    else if (!std::strcmp(what, "REINSTALL")) { ++g_cw.hook_installs; cw_note("HOOK_REINSTALL(post_restore)", pc); }
}
static void bw_cb(void*, const char* why) { cw_invalidate(!std::strcmp(why, "bus_store") ? cw_source_label() : why); }
extern "C" int iw_event_hook(uint32_t pc, int thumb) {
    ++g_cw.hook_calls;
    if (g_cw.valid) { g_runtime_force_interp_hook = nullptr; return 0; }
    if (thumb || pc < kGuardBase || pc >= kGuardBase + kGuardLen) return 0;
    if (g_cw.fail_gen == g_cw.gen) { ++g_cw.cached_rejects; return 1; }   // negative cache: no write has touched the range since the last failed compare
    ++g_cw.fullcmp;
    if (std::memcmp(g_guard_iwram + (kGuardBase - 0x03000000u), g_frozen.data(), kGuardLen) == 0) {
        g_cw.valid = true; ++g_cw.revalidations; cw_note("REVALIDATE(pass)", pc);
        g_runtime_force_interp_hook = nullptr;
        return 0;
    }
    ++g_cw.rejects; g_cw.fail_gen = g_cw.gen;
    if (g_cw.log.empty() || g_cw.log.back().compare(0, 6, "REJECT") != 0) cw_note("REJECT(mismatch)", pc);
    return 1;
}
// Dynamic oracle: "valid" must never be true while the live bytes differ from the frozen image.
static void cw_check_invariant() {
    if (g_guard_on != 3 || !g_cw.valid) return;
    ++g_cw.invariant_checks;
    if (std::memcmp(g_guard_iwram + (kGuardBase - 0x03000000u), g_frozen.data(), kGuardLen) != 0) {
        if (!g_cw.invariant_violations++) { g_cw.first_violation_retired = runtime_fp_count();
            cw_note("INVARIANT_VIOLATION(valid but bytes differ)", g_cpu.R[15]); }
    }
}
static bool g_check_each_tick = false;
static bool g_check_each_tick_fwd() { return g_check_each_tick; }

// ── injectors: drive the REAL writer paths at VBlank boundaries, identically in ref and native ──
struct Inject { uint64_t vbl; std::string kind; uint32_t addr, val, len; bool done = false; uint64_t at_retired = 0; bool by_retired = false; bool by_cycle = false; };
static std::vector<Inject> g_injects;
static const uint32_t kScratch = 0x0203F000u;     // EWRAM scratch (outside code range); source buffers/stubs
static void put_ewram(Machine& m, uint32_t addr, const void* p, size_t n) {
    std::memcpy(const_cast<uint8_t*>(m.bus.ewram_ptr()) + (addr - 0x02000000u), p, n);
}
static uint32_t live_u(Machine& m, uint32_t addr, unsigned nbytes) {      // read live bytes (IWRAM/EWRAM) with mirror fold
    uint32_t v = 0;
    for (unsigned i = 0; i < nbytes; ++i) {
        const uint32_t a = addr + i;
        const uint8_t b = (a >> 24) == 3 ? m.bus.iwram_read_ptr()[a & 0x7FFF] : m.bus.ewram_ptr()[a & 0x3FFFF];
        v |= uint32_t(b) << (8 * i);
    }
    return v;
}
static void build_src(Machine& m, uint32_t dst, uint32_t unit, uint32_t len, uint32_t val) {
    uint8_t buf[256] = {0};
    const uint32_t n = std::min<uint32_t>(unit * len, sizeof buf);
    for (uint32_t i = 0; i < n; ++i) buf[i] = (uint8_t)live_u(m, dst + i, 1);   // copy of live destination => rewrite is a no-op...
    std::memcpy(buf, &val, std::min<uint32_t>(unit, 4));                       // ...except the first unit
    put_ewram(m, kScratch + 0x100, buf, n);
}
static gbarecomp::heal_gate::Journal g_journal;
static void do_inject(Machine& m, Inject& in) {
    const uint8_t* iw = m.bus.iwram_read_ptr();
    const uint32_t lo = kGuardBase - 0x03000000u;
    std::vector<uint8_t> before(iw + lo, iw + lo + kGuardLen);
    ArmCpuState saved = g_cpu; std::string kstr = in.kind; const bool masked = !kstr.empty() && kstr.back() == 'm';
    if (masked) kstr.pop_back();
    const char* kind = kstr.c_str();
    uint32_t val = in.val;
    if (masked) {                                   // bytes selected by mask (in.len) come from val, the rest stay live
        const unsigned nb = (!std::strcmp(kind, "nstore32") || !std::strcmp(kind, "istore32")) ? 4 : 2;
        const uint32_t live = live_u(m, in.addr, nb); uint32_t v = 0;
        for (unsigned i = 0; i < nb; ++i) v |= ((((in.len >> i) & 1u) ? (in.val >> (8 * i)) : (live >> (8 * i))) & 0xFFu) << (8 * i);
        val = v;
    }
    g_label = nullptr;
    auto stage = [&]() { g_cpu.cpsr = 0x9Fu; /* system, IRQ+FIQ masked: no asynchronous entry during the injection */ };
    if (!std::strcmp(kind, "nstore16"))      { g_label = "native_store_bus_write_u16"; bus_write_u16(in.addr, (uint16_t)val); }
    else if (!std::strcmp(kind, "nstore32")) { g_label = "native_store_bus_write_u32"; bus_write_u32(in.addr, val); }
    else if (!std::strcmp(kind, "nstore8"))  { g_label = "native_store_bus_write_u8";  bus_write_u8(in.addr, (uint8_t)val); }
    else if (!std::strncmp(kind, "istore", 6)) {
        // real interpreter: execute ONE ARM store instruction from an EWRAM stub via runtime_force_interp_step
        const uint32_t w = (kind[6] == '8') ? 0xE5C10000u : (kind[6] == '1' ? 0xE1C100B0u : 0xE5810000u);  // strb/strh/str r0,[r1]
        put_ewram(m, kScratch, &w, 4);
        g_label = "interp_store_force_step"; stage();
        g_cpu.R[0] = val; g_cpu.R[1] = in.addr; g_cpu.R[15] = kScratch;
        runtime_force_interp_step();
    } else if (!std::strncmp(kind, "dma", 3) && std::strcmp(kind, "dmacopy")) {
        const bool w32 = kind[3] == '3';
        build_src(m, in.addr, w32 ? 4 : 2, in.len, val);
        g_label = "dma3_immediate_bus";
        bus_write_u32(0x040000D4u, kScratch + 0x100); bus_write_u32(0x040000D8u, in.addr);
        bus_write_u16(0x040000DCu, (uint16_t)in.len); bus_write_u16(0x040000DEu, (uint16_t)(0x8000u | (w32 ? 0x0400u : 0u)));
    } else if (!std::strncmp(kind, "cpuset", 6) || !std::strcmp(kind, "fastset")) {
        const bool fast = !std::strcmp(kind, "fastset"); const bool w32 = fast || kind[6] == '3';
        build_src(m, in.addr, w32 ? 4 : 2, in.len, val);
        g_label = fast ? "bios_hle_cpufastset" : "bios_hle_cpuset"; stage();
        g_cpu.R[0] = kScratch + 0x100; g_cpu.R[1] = in.addr; g_cpu.R[2] = in.len | ((w32 && !fast) ? 0x04000000u : 0u);
        g_cpu.R[15] = kScratch + 4;
        runtime_swi((fast ? 0x0Cu : 0x0Bu) << 16);
    } else if (!std::strncmp(kind, "bstore", 6)) {
        // real interpreter-bridge loop: Interpreter::step(cpu, *GbaBus, ...) with direct GbaBus access (not ForceInterpBus)
        const uint32_t w = (kind[6] == '8') ? 0xE5C10000u : (kind[6] == '1' ? 0xE1C100B0u : 0xE5810000u);
        put_ewram(m, kScratch, &w, 4);
        g_label = "bridge_interpreter_store"; stage();
        g_cpu.R[0] = val; g_cpu.R[1] = in.addr; g_cpu.R[15] = kScratch;
        runtime_bridge_interpret(kScratch, false, kScratch + 4, 0);
    } else if (!std::strcmp(kind, "lz77")) {
        uint8_t buf[16] = {0x10, 4, 0, 0, 0x00, 0, 0, 0, 0}; std::memcpy(buf + 5, &val, 4);
        put_ewram(m, kScratch + 0x100, buf, 9);
        g_label = "bios_hle_lz77_wram"; stage();
        g_cpu.R[0] = kScratch + 0x100; g_cpu.R[1] = in.addr; g_cpu.R[15] = kScratch + 4;
        runtime_swi(0x11u << 16);
    } else if (!std::strcmp(kind, "dmacopy")) {
        build_src(m, in.addr, 4, in.len, val);
        g_label = "GbaBus::dma_copy_ram";
        const bool ok = m.bus.dma_copy_ram(kScratch + 0x100, in.addr, in.len * 4);
        if (!ok) for (uint32_t k = 0; k < in.len; ++k)            // GbaIo falls back to per-element bus writes when the bulk copy declines
            m.bus.write32(in.addr + 4 * k, m.bus.read32(kScratch + 0x100 + 4 * k));
        std::printf("DMA_COPY_RAM returned %d (observer installed=%d)\n", (int)ok, g_guard_on == 3);
    } else if (!std::strcmp(kind, "rramreset")) {
        g_label = "bios_lle_RegisterRamReset"; stage();
        g_cpu.R[0] = val; g_cpu.R[15] = kScratch + 4;
        runtime_swi(0x01u << 16);
    } else if (!std::strcmp(kind, "healrestore")) {
        // heal_gate::restore_full: whole-region memcpy over IWRAM (new writer found by the deleted-accessor enumeration)
        g_label = "heal_gate_restore_full";
        auto snap = gbarecomp::heal_gate::capture_full(g_cpu, g_runtime_cycles, m.bus);
        snap.iwram[in.addr & 0x7FFFu] = (uint8_t)val;
        ArmCpuState tcpu = g_cpu; uint64_t tcyc = g_runtime_cycles;
        gbarecomp::heal_gate::restore_full(snap, tcpu, tcyc, m.bus);
    } else if (!std::strcmp(kind, "jmut")) {
        // plain bus store of the mutated byte, then journalled bus store of the ORIGINAL byte (journal old_value = mutated)
        g_label = "native_store_bus_write_u8"; bus_write_u8(in.addr, (uint8_t)val);
        g_journal.clear(); g_journal.arm(m.bus, gbarecomp::heal_gate::Journal::Mode::Record);
        bus_write_u8(in.addr, g_frozen[(in.addr & 0x7FFFu) - (kGuardBase - 0x03000000u)]);
        g_journal.disarm(m.bus);
    } else if (!std::strcmp(kind, "jrollback")) {
        // raw rollback re-applies the mutated byte (journal old_value) with no bus store
        g_label = "heal_gate_rollback"; g_journal.rollback(m.bus);
    } else if (!std::strcmp(kind, "ptrpoke")) {   // RED CONTROL: bypasses every observer (like a bulk/debug writer)
        g_label = "ptrpoke_bypass";
        const_cast<uint8_t*>(iw)[in.addr & 0x7FFFu] = (uint8_t)val;
    } else if (!std::strcmp(kind, "invalidate")) { cw_invalidate("explicit_bulk_invalidate"); }
    g_cpu = saved; g_label = nullptr;
    std::vector<uint8_t> after(iw + lo, iw + lo + kGuardLen);
    unsigned d = 0; for (uint32_t i = 0; i < kGuardLen; ++i) d += before[i] != after[i];
    // oracle: a change to the code range must have left cw invalid (unless this is the bypass red control)
    const bool oracle_ok = (d == 0) || !g_cw.valid || g_guard_on != 3;
    std::printf("INJECT vbl=%llu retired=%u kind=%s addr=%08x val=%08x len=%u code_bytes_changed=%u cw_valid=%d gen=%llu oracle=%s\n",
                (unsigned long long)in.vbl, runtime_fp_count(), in.kind.c_str(), in.addr, val, in.len, d, (int)g_cw.valid,
                (unsigned long long)g_cw.gen, oracle_ok ? "OK" : "STALE_VALID");
}
static void apply_injects(Machine& m, uint64_t rel_vbl) {
    for (auto& in : g_injects) if (!in.done && !in.by_retired && in.vbl <= rel_vbl) { in.done = true; do_inject(m, in); cw_check_invariant(); }
}
static Machine* g_inj_machine = nullptr; static bool g_in_tick_inject = false;
static void tick_injects() {      // called from the runtime_tick wrapper: an exact retired-instruction boundary, any context
    if (g_in_tick_inject || !g_inj_machine) return;
    for (auto& in : g_injects) if (!in.done && in.by_retired && (in.by_cycle ? (unsigned long long)g_runtime_cycles >= in.at_retired : runtime_fp_count() >= in.at_retired)) {
        g_in_tick_inject = true; in.done = true; do_inject(*g_inj_machine, in); cw_check_invariant(); g_in_tick_inject = false;
    }
}
static void print_cw_stats() {
    if (g_lib_guard) { const auto& L = gbarecomp::iwram_code_guard_stats(); g_cw.hook_calls = L.hook_calls; g_cw.fullcmp = L.full_compares; g_cw.cached_rejects = L.cached_rejects; g_cw.hook_installs = L.hook_installs; }
    std::printf("CW_STATS valid_end=%d stores_seen=%llu iwram_stores=%llu overlap_stores=%llu noop_overlap=%llu invalidations=%llu "
                "hook_installs=%llu hook_calls=%llu full_compares=%llu revalidations=%llu rejects=%llu cached_rejects=%llu invariant_checks=%llu invariant_violations=%llu\n",
                (int)g_cw.valid, (unsigned long long)g_cw.seen, (unsigned long long)g_cw.iw_seen, (unsigned long long)g_cw.overlap_seen,
                (unsigned long long)g_cw.noop_overlap, (unsigned long long)g_cw.invalidations, (unsigned long long)g_cw.hook_installs,
                (unsigned long long)g_cw.hook_calls, (unsigned long long)g_cw.fullcmp, (unsigned long long)g_cw.revalidations,
                (unsigned long long)g_cw.rejects, (unsigned long long)g_cw.cached_rejects, (unsigned long long)g_cw.invariant_checks, (unsigned long long)g_cw.invariant_violations);
    for (auto& kv : g_cw.by_src) std::printf("CW_INVALIDATIONS_BY_SOURCE %s %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
    for (auto& l : g_cw.log) std::printf("CW_EVENT %s\n", l.c_str());
}
static const char* cw_source_label() {
    if (g_label) return g_label;
    if (g_in_swi) return "swi_hle_path";
    const int c = g_ctx.empty() ? 0 : g_ctx.back();
    return c ? "interp_or_bridge_store(unlabelled)" : "generated_native_store(unlabelled)";
}

static void dump_events(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    for (const auto& e : g_ev)
        std::fprintf(f, "seq=%u cyc=%llu %s a=%08x pc=%08x cpsr=%08x nest=%u depth=%u vbl=%u yv=%u\n",
                     e.seq, e.cyc, ev_name(e.kind), e.a, e.pc, e.cpsr, e.nest, e.depth, e.vbl, e.yv);
    std::fclose(f);
}

static void snap(Machine& m, const char* tag, uint64_t n) {
    auto d = gba::save_device_state(m.bus, m.ppu);
    uint32_t ob; std::memcpy(&ob, &d[0x60809], 4);
    for (int i = 0; i < 5; ++i) d[0x60809 + i] = 0;   // latch reported separately
    const uint64_t dh = fnv(kFnvOff, d.data(), d.size());
    const uint64_t iw = fnv(kFnvOff, m.bus.iwram_read_ptr(), 32u * 1024u);
    const uint64_t ew = fnv(kFnvOff, m.bus.ewram_ptr(), 256u * 1024u);
    gbarecomp::RuntimeTimingContext tc{};
    gbarecomp::runtime_capture_timing_context(tc);
    std::printf("SNAP %s n=%llu retired=%u pc=%08x cpsr=%08x cyc=%llu pend=%llu budget=%lld vbl=%llu yv=%llu "
                "IE=%04x IF=%04x IME=%04x dev=%016llx iw=%016llx ew=%016llx ob=%08x r:",
                tag, (unsigned long long)n, runtime_fp_count(), g_cpu.R[15], g_cpu.cpsr,
                (unsigned long long)tc.cycles, (unsigned long long)tc.pending_cycles, (long long)tc.event_budget,
                (unsigned long long)tc.vblank_starts, (unsigned long long)tc.yielded_vblank,
                m.bus.io().read16(0x200), m.bus.io().read16(0x202), m.bus.io().read16(0x208),
                (unsigned long long)dh, (unsigned long long)iw, (unsigned long long)ew, ob);
    for (int i = 0; i < 16; ++i) std::printf(" %x", g_cpu.R[i]);
    std::printf("\n");
}
static uint32_t open_bus_of(Machine& m) {
    auto d = gba::save_device_state(m.bus, m.ppu);
    uint32_t v; std::memcpy(&v, &d[0x60809], 4); return v;
}

// mode: "native" or "ref". Runs `vblanks` VBlank starts (counted like the harness's
// frame_mode), logging events + writing the per-instruction fingerprint ring.
static int trace_run(const std::string& rom_path, const std::string& bios_path,
                     const std::string& ck_path, const std::string& mode, uint64_t vblanks,
                     uint64_t insn_cap, const std::string& prefix) {
    std::vector<uint8_t> rom, ck;
    { std::ifstream f(rom_path, std::ios::binary); rom.assign(std::istreambuf_iterator<char>(f), {}); }
    { std::ifstream f(ck_path, std::ios::binary); ck.assign(std::istreambuf_iterator<char>(f), {}); }
    gba::GbaBios bios;
    { std::string e; if (!bios.load_from_file(bios_path, kBiosSha1, &e)) { std::printf("bios load fail %s\n", e.c_str()); return 2; } }
    Machine m;
    const bool native = mode == "native";
    g_force_interp = (native && !g_island) ? 0 : 1;
    g_runtime_native_island = (native && g_island && !g_island_off && !g_island_keep) ? 1 : 0;   // island mode applies to EVERY native run, guarded or not (red controls included)
    std::string err;
    if (g_bus_watch && g_guard_on == 3 && native) { g_cw = CodeWatch{}; g_label = nullptr;
        if (g_lib_guard) { cold_boot(m, rom, bios, "", true);   /* attach the ROM (production attaches it before HostWindow::open) */
          if (!sma2::iwram_aot_install(m.bus)) return 3; if (!g_island_keep) g_runtime_native_island = (g_island && !g_island_off) ? 1 : 0; gbarecomp::iwram_code_guard_set_event_sink(&lib_ev); lib_ev("INVALIDATE", "install", 0); }
        else m.bus.set_iwram_code_watch(kGuardBase - 0x03000000u, kGuardBase - 0x03000000u + kGuardLen, &bw_cb, nullptr); }
    if (!restore_checkpoint_gated(m, rom, bios, ck_path, ck, &err)) {
        std::printf("restore fail %s\n", err.c_str()); return 2;
    }
    if (native) { gbarecomp::self_heal_reset(); g_runtime_fn_entry_hook = &native_replay_entry_hook; }
    // Checkpoint restore resets the ARM context (incl. the force-interp hook): the
    // guard is (re)installed AFTER every restore, and holds no state to carry over.
    g_guard_iwram = m.bus.iwram_read_ptr();
    g_gs = GuardStats{};
    g_poke_iwram = const_cast<uint8_t*>(m.bus.iwram_read_ptr());
    if (native && g_guard_on == 3) {
        // event-driven model: observer on the bus + explicit invalidate for the bulk restore
        if (g_bus_watch) {
            // library did the invalidation inside deserialize(); do NOT reset g_cw here
            if (!g_lib_guard && !g_cw.valid) g_runtime_force_interp_hook = &iw_event_hook;   // harness reinstall (non-lib mode)
            if (g_skip_restore_invalidate) { g_cw.valid = true; if (g_lib_guard) gbarecomp::iwram_code_guard_test_assume_valid(); cw_note("ASSUME_VALID(no restore invalidation: red control)", 0); }
        } else {
        m.bus.set_write_observer(&g_cw);
        g_cw = CodeWatch{}; g_label = nullptr;
        if (!g_skip_restore_invalidate) cw_invalidate("checkpoint_restore");
        else { g_cw.valid = true; cw_note("ASSUME_VALID(no restore invalidation: red control)", 0); }
        }
    } else if (native && (g_guard_on || !g_rpokes.empty())) g_runtime_force_interp_hook = &iw_guard_hook;
    if (g_check_each_tick_env()) g_check_each_tick = true;
    for (auto& pk : g_pokes) pk.done = false;
    for (auto& pk : g_rpokes) pk.done = false;
    arm_ring();
    g_ws_armed = false;
    if (!g_dump_ppm.empty()) { std::vector<uint8_t> fb(m.ppu.render_bytes()); m.ppu.render(fb.data(), m.bus.io().read16(0x000), m.bus.io().raw(), m.bus.vram_ptr(), m.bus.oam_ptr(), m.bus.pal_ptr());
        std::ofstream pf(g_dump_ppm, std::ios::binary); pf << "P6\n" << m.ppu.render_width() << " " << m.ppu.render_height() << "\n255\n"; pf.write((const char*)fb.data(), fb.size()); std::printf("DUMPED_PPM %s\n", g_dump_ppm.c_str()); }
    if (g_cls) m.ppu.set_presentation_observer(&g_clsobs);
    gbarecomp::RuntimeTimingContext tc0{};
    gbarecomp::runtime_capture_timing_context(tc0);
    const uint64_t vb0 = tc0.vblank_starts;
    std::printf("TRACE_FRAME ppu_frame=%llu\n", (unsigned long long)m.ppu.frame_count()); std::printf("TRACE_START mode=%s pc=%08x cpsr=%08x vbl=%llu yielded=%llu cycles=%llu ob=%08x\n",
                mode.c_str(), g_cpu.R[15], g_cpu.cpsr, (unsigned long long)tc0.vblank_starts,
                (unsigned long long)tc0.yielded_vblank, (unsigned long long)tc0.cycles, open_bus_of(m));
    g_ob_bus = &m.bus; g_ob_last = m.bus.open_bus_pc_;
    g_tag_on = native; g_tags.clear(); g_ctx.clear(); g_fn_entries.clear(); g_fn_entry_calls = 0;
    g_ev_on = true; g_inj_machine = &m; for (auto& in : g_injects) in.done = false;
    apply_pokes(m, 0); apply_injects(m, 0);
    uint32_t last_ob = open_bus_of(m);
    uint64_t steps = 0;
    uint64_t last_yv = tc0.yielded_vblank;
    const auto t_loop0 = std::chrono::steady_clock::now();
    for (;;) {
        gbarecomp::RuntimeTimingContext tc{};
        gbarecomp::runtime_capture_timing_context(tc);
        if (tc.vblank_starts >= vb0 + vblanks && tc.yielded_vblank >= tc.vblank_starts) break;
        if (runtime_fp_count() >= insn_cap) break;
        const uint32_t pc = g_cpu.R[15];
        if (!native && !g_rpokes.empty()) apply_rpokes(runtime_fp_count());
        const uint32_t seq0 = runtime_fp_count();
        log_ev(EV_STEP_IN, pc);
        step_once(m, native && !g_island);
        apply_replay(m.bus, m.ppu.frame_count());
        log_ev(EV_STEP_OUT, runtime_fp_count() - seq0);
        gbarecomp::runtime_capture_timing_context(tc);
        if (tc.yielded_vblank != last_yv) { log_ev(EV_YIELD, (uint32_t)tc.yielded_vblank); last_yv = tc.yielded_vblank; snap(m, "yield", tc.yielded_vblank - vb0);
            for (auto& w : g_watch) { const uint64_t h = fnv(kFnvOff, m.bus.iwram_read_ptr() + w.lo, w.hi - w.lo); if (!w.seen || h != w.last) { std::printf("WATCH %04x-%04x v=%llu frame=%llu hash=%016llx %s\n", w.lo, w.hi, (unsigned long long)(tc.yielded_vblank - vb0), (unsigned long long)m.ppu.frame_count(), (unsigned long long)h, w.seen ? "CHANGED" : "initial"); w.last = h; w.seen = true; } }
            if (g_digest) { uint64_t fh = kFnvOff; if (m.ppu.has_latched_framebuffer()) fh = fnv(kFnvOff, m.ppu.latched_framebuffer(), m.ppu.render_bytes());
                std::printf("DIGEST v=%llu frame=%llu n=%llu h=%016llx iw=%llu mixer=%llu fb=%016llx\n", (unsigned long long)(tc.yielded_vblank - vb0), (unsigned long long)m.ppu.frame_count(), (unsigned long long)g_dg_n, (unsigned long long)g_dg_h, (unsigned long long)g_dg_iwram, (unsigned long long)g_dg_mixer, (unsigned long long)fh); }
            if (!g_audio_log.empty()) { static std::ofstream al(g_audio_log); const uint8_t* io = m.bus.io().raw(); char b[256];
                std::snprintf(b, sizeof b, "%llu psg=%016llx wave=%016llx cnt=%04x%04x%04x dma1=%08x/%04x dma2=%08x/%04x\n", (unsigned long long)m.ppu.frame_count(),
                    (unsigned long long)fnv(kFnvOff, io + 0x60, 0x20), (unsigned long long)fnv(kFnvOff, io + 0x90, 0x10), (io[0x82] | io[0x83] << 8), (io[0x80] | io[0x81] << 8), (io[0x84] | io[0x85] << 8),
                    (io[0xBC] | io[0xBD] << 8 | io[0xBE] << 16 | io[0xBF] << 24), (io[0xC6] | io[0xC7] << 8), (io[0xC8] | io[0xC9] << 8 | io[0xCA] << 16 | io[0xCB] << 24), (io[0xD2] | io[0xD3] << 8)); al << b;
                const uint32_t sad = (io[0xBC] | io[0xBD] << 8 | io[0xBE] << 16 | io[0xBF] << 24) - 0x03000000u; double ea = 0; int zc = 0; int8_t prev = 0; uint64_t h = kFnvOff;   // PCM envelope of the DMA1 FIFO source buffer (direct-sound ch A)
                if (sad < 0x7000) { const int8_t* pcm = (const int8_t*)m.bus.iwram_read_ptr() + sad; for (int k = 0; k < 1584; ++k) { ea += std::abs((int)pcm[k]); if ((pcm[k] < 0) != (prev < 0)) ++zc; prev = pcm[k]; } h = fnv(kFnvOff, pcm, 1584); }
                std::snprintf(b, sizeof b, "PCM %llu meanabs=%.2f zc=%d h=%016llx\n", (unsigned long long)m.ppu.frame_count(), ea / 1584.0, zc, (unsigned long long)h); al << b; }
            for (uint64_t cf : g_capture_frames) if (cf == m.ppu.frame_count()) { std::vector<uint8_t> img; std::string cerr; char nm[512]; std::snprintf(nm, sizeof nm, "%s_%llu.gbas", g_capture_frames_prefix.c_str(), (unsigned long long)cf);
                if (capture_h5_image(m, &img, &cerr)) { std::ofstream cfo(nm, std::ios::binary); cfo.write((const char*)img.data(), img.size()); std::printf("CAPTURED_FRAME frame=%llu file=%s\n", (unsigned long long)cf, nm); } else std::printf("CAPTURE_REFUSED_FRAME frame=%llu reason=%s\n", (unsigned long long)cf, cerr.c_str()); }
            if (g_capture_every && (tc.yielded_vblank - vb0) % g_capture_every == 0) {
                std::vector<uint8_t> img; std::string cerr; char nm[512]; std::snprintf(nm, sizeof nm, "%s_%llu.gbas", g_capture_every_prefix.c_str(), (unsigned long long)m.ppu.frame_count());
                if (capture_h5_image(m, &img, &cerr)) { std::ofstream cf(nm, std::ios::binary); cf.write((const char*)img.data(), img.size()); std::printf("CAPTURED_EVERY frame=%llu bytes=%zu file=%s\n", (unsigned long long)m.ppu.frame_count(), img.size(), nm); }
                else std::printf("CAPTURE_REFUSED_EVERY frame=%llu reason=%s\n", (unsigned long long)m.ppu.frame_count(), cerr.c_str());
            }
            if (g_capture_at && tc.yielded_vblank - vb0 == g_capture_at) {
                std::vector<uint8_t> img; std::string cerr;
                if (capture_h5_image(m, &img, &cerr)) { std::ofstream cf(g_capture_file, std::ios::binary); cf.write((const char*)img.data(), img.size()); std::printf("CAPTURED vbl=%llu bytes=%zu file=%s\n", (unsigned long long)g_capture_at, img.size(), g_capture_file.c_str()); }
                else std::printf("CAPTURE_REFUSED vbl=%llu reason=%s\n", (unsigned long long)g_capture_at, cerr.c_str());
            } apply_pokes(m, tc.yielded_vblank - vb0); cw_check_invariant(); apply_injects(m, tc.yielded_vblank - vb0); }
        ++steps;
        if (steps > g_step_guard) { std::printf("step guard\n"); break; }
    }
    std::printf("LOOP_SECONDS %.6f\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t_loop0).count());
    if (!g_ckpt2.empty()) {
        // in-process checkpoint restore (phase 2): replaces ALL guest memory through the bulk path
        std::vector<uint8_t> ck2; { std::ifstream f(g_ckpt2, std::ios::binary); ck2.assign(std::istreambuf_iterator<char>(f), {}); }
        std::printf("PHASE2_RESTORE %s native_cw_valid_before=%d\n", g_ckpt2.c_str(), (int)g_cw.valid);
        if (!restore_checkpoint_gated(m, rom, bios, g_ckpt2, ck2, &err)) { std::printf("restore2 fail %s\n", err.c_str()); return 2; }
        if (native) {
            g_runtime_fn_entry_hook = &native_replay_entry_hook;
            g_guard_iwram = m.bus.iwram_read_ptr();
            if (g_guard_on == 3 && g_bus_watch) {
                if (!g_lib_guard && !g_cw.valid) g_runtime_force_interp_hook = &iw_event_hook;
                if (g_skip_restore_invalidate) { g_cw.valid = true; if (g_lib_guard) gbarecomp::iwram_code_guard_test_assume_valid(); cw_note("ASSUME_STILL_VALID(no restore invalidation: red control)", 0); }
            } else if (g_guard_on == 3) {
                m.bus.set_write_observer(&g_cw);
                if (!g_skip_restore_invalidate) cw_invalidate("checkpoint_restore");
                else cw_note("ASSUME_STILL_VALID(no restore invalidation: red control)", 0);
            } else if (g_guard_on) g_runtime_force_interp_hook = &iw_guard_hook;
        }
        gbarecomp::RuntimeTimingContext tcb{}; gbarecomp::runtime_capture_timing_context(tcb);
        const uint64_t vb1 = tcb.vblank_starts; uint64_t lyv = tcb.yielded_vblank; uint64_t st2 = 0;
        for (;;) {
            gbarecomp::RuntimeTimingContext t2{}; gbarecomp::runtime_capture_timing_context(t2);
            if (t2.vblank_starts >= vb1 + g_vblanks2 && t2.yielded_vblank >= t2.vblank_starts) break;
            step_once(m, native && !g_island);
            gbarecomp::runtime_capture_timing_context(t2);
            if (t2.yielded_vblank != lyv) { lyv = t2.yielded_vblank; snap(m, "yield2", t2.yielded_vblank - vb1); cw_check_invariant(); }
            if (++st2 > 5'000'000) { std::printf("step guard 2\n"); break; }
        }
    }
    g_ev_on = false;
    if (g_final_snap) snap(m, "final", 0);
    gbarecomp::RuntimeTimingContext tc{};
    gbarecomp::runtime_capture_timing_context(tc);
    if (g_ihist) { for (auto& kv : g_interp_hist) std::printf("INTERP_IN_RANGE pc=%08x count=%llu\n", kv.first, (unsigned long long)kv.second); for (auto& kv : g_interp_out_hist) std::printf("INTERP_IWRAM_OUTSIDE pc_T=%08x count=%llu\n", kv.first, (unsigned long long)kv.second); for (auto& e : g_ihe) std::printf("INTERP_IN_RANGE_EVENT vbl=%llu cyc=%llu pc=%08x lr=%08x cpsr=%08x\n", (unsigned long long)e.vbl, (unsigned long long)e.cyc, e.pc, e.lr, e.cpsr); }
    if (g_digest) std::printf("DIGEST_END n=%llu h=%016llx iwram=%llu mixer=%llu native_instr=%llu native_mixer=%llu\n", (unsigned long long)g_dg_n, (unsigned long long)g_dg_h, (unsigned long long)g_dg_iwram, (unsigned long long)g_dg_mixer, (unsigned long long)g_dg_native, (unsigned long long)g_dg_native_mixer);
    std::printf("TRACE_END mode=%s retired=%u steps=%llu pc=%08x vbl=%llu yielded=%llu cycles=%llu ob=%08x events=%zu\n",
                mode.c_str(), runtime_fp_count(), (unsigned long long)steps, g_cpu.R[15],
                (unsigned long long)tc.vblank_starts, (unsigned long long)tc.yielded_vblank,
                (unsigned long long)tc.cycles, open_bus_of(m), g_ev.size());
    (void)last_ob;
    runtime_fp_save_file((prefix + "." + mode + ".fp").c_str());
    if (native) {
        FILE* tf = std::fopen((prefix + ".tags").c_str(), "wb");
        std::fwrite(g_tags.data(), 1, g_tags.size(), tf); std::fclose(tf);
        FILE* ef = std::fopen((prefix + ".entries").c_str(), "w");
        for (const auto& kv : g_fn_entries) std::fprintf(ef, "%08x %llu\n", kv.first, (unsigned long long)kv.second);
        std::fclose(ef);
        std::printf("PROFILE_TAGS count=%zu fn_entry_hook_calls=%llu distinct_entries=%zu self_heal_interpreted=%llu\n",
                    g_tags.size(), (unsigned long long)g_fn_entry_calls, g_fn_entries.size(),
                    (unsigned long long)gbarecomp::self_heal_interpreted_insns());
    }
    dump_events(prefix + "." + mode + ".ev");
    std::printf("ISLAND_PROBE wants(030015d8)=%d wants(030015f4)=%d wants(030016b8)=%d wants(08000000)=%d\n", runtime_island_wants_native(0x030015D8u,0), runtime_island_wants_native(0x030015F4u,0), runtime_island_wants_native(0x030016B8u,0), runtime_island_wants_native(0x08000000u,0));
    std::printf("ISLAND_STATE island=%d force_interp=%d static_entry_030015d8=%d hook=%p valid=%d\n", g_runtime_native_island, g_force_interp,
                runtime_has_static_entry(0x030015D8u, 0), (void*)g_runtime_force_interp_hook, (int)gbarecomp::iwram_code_guard_valid());
    if (native && g_guard_on == 3) print_cw_stats();
    if (g_ihist && !native) std::printf("REF_FIS_CALLS %llu\n", (unsigned long long)g_wrap_fis_calls);
    if (native) { print_guard_stats(); std::printf("WRAP_BRIDGE_INTERPRET_CALLS %llu FORCE_INTERP_STEP_CALLS %llu\n", (unsigned long long)g_wrap_bridge_calls, (unsigned long long)g_wrap_fis_calls); }
    { auto iw = m.bus.iwram_read_ptr(); std::printf("IWRAM_CODE_HASH_END %016llx frozen_match=%d\n",
        (unsigned long long)fnv(kFnvOff, iw + (kGuardBase - 0x03000000u), kGuardLen),
        (int)(std::memcmp(iw + (kGuardBase - 0x03000000u), g_frozen.data(), kGuardLen) == 0)); }
    { auto dumpf = [&](const char* ext, const uint8_t* p, size_t n) { FILE* f = std::fopen((prefix + "." + mode + ext).c_str(), "wb"); std::fwrite(p, 1, n, f); std::fclose(f); };
      dumpf(".iwram", m.bus.iwram_read_ptr(), 32768); dumpf(".ewram", m.bus.ewram_ptr(), 262144); }
    g_runtime_insn_trace = 0u;
    return 0;
}

// Restore a checkpoint, poke bytes, recapture, write a derived checkpoint file.
static int make_altered(const std::string& rom_path, const std::string& bios_path,
                        const std::string& ck_path, const std::string& out_path) {
    std::vector<uint8_t> rom, ck;
    { std::ifstream f(rom_path, std::ios::binary); rom.assign(std::istreambuf_iterator<char>(f), {}); }
    { std::ifstream f(ck_path, std::ios::binary); ck.assign(std::istreambuf_iterator<char>(f), {}); }
    gba::GbaBios bios; { std::string e; if (!bios.load_from_file(bios_path, kBiosSha1, &e)) return 2; }
    Machine m; g_force_interp = 1; std::string err;
    if (!restore_checkpoint_gated(m, rom, bios, ck_path, ck, &err)) { std::printf("restore fail %s\n", err.c_str()); return 2; }
    apply_pokes(m, 0);
    std::vector<uint8_t> img;
    if (!capture_h5_image(m, &img, &err)) { std::printf("capture refused %s\n", err.c_str()); return 2; }
    FILE* f = std::fopen(out_path.c_str(), "wb"); std::fwrite(img.data(), 1, img.size(), f); std::fclose(f);
    std::printf("ALTERED_CKPT_WRITTEN %s bytes=%zu\n", out_path.c_str(), img.size());
    return 0;
}


// ── IWRAM provenance watch (read-only): forced-interpreter, one instruction per
// step. Shadow-compares IWRAM after every step and aggregates changes by the PC
// that executed. `ck` empty = cold boot (app_boot_skip), else restore checkpoint.
static int iwram_watch(const std::string& rom_path, const std::string& bios_path,
                       const std::string& ck_path, uint64_t frames, const std::string& prefix,
                       uint32_t lo, uint32_t hi) {
    std::vector<uint8_t> rom, ck;
    { std::ifstream f(rom_path, std::ios::binary); rom.assign(std::istreambuf_iterator<char>(f), {}); }
    gba::GbaBios bios; { std::string e; if (!bios.load_from_file(bios_path, kBiosSha1, &e)) return 2; }
    Machine m; g_force_interp = 1; std::string err;
    if (ck_path.empty()) cold_boot(m, rom, bios, "", true);
    else { { std::ifstream f(ck_path, std::ios::binary); ck.assign(std::istreambuf_iterator<char>(f), {}); }
           if (!restore_checkpoint_gated(m, rom, bios, ck_path, ck, &err)) { std::printf("restore fail %s\n", err.c_str()); return 2; } }
    const uint8_t* iw = m.bus.iwram_read_ptr();
    std::vector<uint8_t> shadow(iw, iw + 32768);
    auto dump = [&](const std::string& n) { FILE* f = std::fopen(n.c_str(), "wb"); std::fwrite(iw, 1, 32768, f); std::fclose(f); };
    dump(prefix + ".start.iwram");
    struct W { uint64_t n = 0, bytes = 0, first_insn = 0, first_frame = 0; uint32_t amin = 0xFFFFFFFF, amax = 0; uint32_t r[4]{}; uint32_t lr = 0; };
    std::map<uint32_t, W> writers, cw;   // all writers; writers into [lo,hi)
    const uint64_t f0 = m.ppu.frame_count(); uint64_t insn = 0;
    std::map<uint64_t, int> frame_code_changes;
    while (m.ppu.frame_count() < f0 + frames) {
        const uint32_t pc = g_cpu.R[15]; uint32_t r[4] = {g_cpu.R[0], g_cpu.R[1], g_cpu.R[2], g_cpu.R[3]}; const uint32_t lr = g_cpu.R[14];
        step_once(m, false); ++insn;
        if (std::memcmp(shadow.data(), iw, 32768) != 0) {
            for (uint32_t i = 0; i < 32768;) {
                if (shadow[i] == iw[i]) { ++i; continue; }
                uint32_t j = i; while (j < 32768 && shadow[j] != iw[j]) ++j;
                auto up = [&](std::map<uint32_t, W>& mp) {
                    W& w = mp[pc]; if (w.n == 0) { w.first_insn = insn; w.first_frame = m.ppu.frame_count() - f0; std::memcpy(w.r, r, 16); w.lr = lr; }
                    ++w.n; w.bytes += j - i; w.amin = std::min(w.amin, 0x03000000u + i); w.amax = std::max(w.amax, 0x03000000u + j);
                };
                up(writers);
                if (0x03000000u + i < hi && 0x03000000u + j > lo) { up(cw); ++frame_code_changes[m.ppu.frame_count() - f0]; }
                i = j;
            }
            std::memcpy(shadow.data(), iw, 32768);
        }
    }
    dump(prefix + ".end.iwram");
    std::printf("IWATCH insns=%llu frames=%llu start_frame=%llu\n", (unsigned long long)insn, (unsigned long long)frames, (unsigned long long)f0);
    auto pr = [&](const char* t, std::map<uint32_t, W>& mp) {
        std::printf("%s (%zu writer pcs)\n", t, mp.size());
        for (auto& kv : mp) std::printf("  pc=%08x n=%llu bytes=%llu addr=[%08x,%08x) first_insn=%llu first_frame=%llu r0-3=%08x %08x %08x %08x lr=%08x\n",
            kv.first, (unsigned long long)kv.second.n, (unsigned long long)kv.second.bytes, kv.second.amin, kv.second.amax,
            (unsigned long long)kv.second.first_insn, (unsigned long long)kv.second.first_frame, kv.second.r[0], kv.second.r[1], kv.second.r[2], kv.second.r[3], kv.second.lr);
    };
    pr("WRITERS_INTO_WINDOW", cw);
    if (writers.size() < 80) pr("ALL_IWRAM_WRITERS", writers); else std::printf("ALL_IWRAM_WRITERS %zu pcs (omitted)\n", writers.size());
    for (auto& kv : frame_code_changes) std::printf("WINDOW_CHANGES frame+%llu count=%d\n", (unsigned long long)kv.first, kv.second);
    return 0;
}

// Exhaustive single-byte-flip self-test of the guard predicate on a synthetic IWRAM image.
static int selftest_guard() {
    std::vector<uint8_t> iw(32768, 0);
    g_guard_iwram = iw.data();
    uint8_t* code = iw.data() + (kGuardBase - 0x03000000u);
    std::memcpy(code, g_frozen.data(), kGuardLen);
    int bad = 0, flips = 0;
    auto chk = [&](uint32_t pc, int th, int want, const char* what) {
        const int got = iw_guard_hook(pc, th);
        if (got != want) { ++bad; std::printf("SELFTEST_FAIL %s pc=%08x want=%d got=%d\n", what, pc, want, got); }
    };
    chk(0x030015D8u, 0, 0, "pristine");
    for (uint32_t off = 0; off < kGuardLen; ++off) {
        for (int bit : {0, 7}) {
            code[off] ^= (uint8_t)(1u << bit); ++flips;
            chk(0x030015D8u, 0, 1, "flip->reject");
            chk(kGuardBase, 0, 1, "flip->reject@first_pc");
            chk(kGuardBase + kGuardLen - 4, 0, 1, "flip->reject@last_pc");
            code[off] ^= (uint8_t)(1u << bit);
            chk(0x030015D8u, 0, 0, "restored->pass");
        }
    }
    // bytes just outside the guarded range never affect the verdict
    iw[kGuardBase - 0x03000000u - 1] ^= 0xFF; iw[kGuardBase - 0x03000000u + kGuardLen] ^= 0xFF;
    chk(0x030015D8u, 0, 0, "outside-bytes-ignored");
    chk(kGuardBase - 4, 0, 0, "pc-below-range-not-guarded");
    chk(kGuardBase + kGuardLen, 0, 0, "pc-above-range-not-guarded");
    code[5] ^= 1; chk(0x030015D8u, 1, 0, "thumb-pc-not-judged-here"); code[5] ^= 1;
    std::printf("SELFTEST_GUARD flips=%d (x4 checks) failures=%d %s\n", flips, bad, bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
    std::string rom, bios, ck, mode = "native", prefix, altered_out; uint64_t vbl = 2, cap = 100000000ull; uint32_t lo = 0x03001500, hi = 0x03001800;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--rom") rom = argv[++i]; else if (a == "--bios") bios = argv[++i];
        else if (a == "--ckpt") ck = argv[++i]; else if (a == "--mode") mode = argv[++i];
        else if (a == "--vblanks") vbl = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--cap") cap = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--prefix") prefix = argv[++i];
        else if (a == "--iwatch") { mode = "iwatch"; }
        else if (a == "--bus-watch") g_bus_watch = true;
        else if (a == "--island") g_island = true;
        else if (a == "--capture-every") { std::string v = argv[++i]; auto c = v.find(':'); g_capture_every = std::strtoull(v.substr(0, c).c_str(), nullptr, 10); g_capture_every_prefix = v.substr(c + 1); }
        else if (a == "--pvr-classify") g_cls = true;
        else if (a == "--dump-ppm") g_dump_ppm = argv[++i];
        else if (a == "--audio-log") g_audio_log = argv[++i];
        else if (a == "--capture-frames") { std::string v = argv[++i]; auto c = v.find(':'); std::string l = v.substr(0, c); g_capture_frames_prefix = v.substr(c + 1); size_t q = 0; while (q < l.size()) { size_t e = l.find(',', q); if (e == std::string::npos) e = l.size(); g_capture_frames.push_back(std::strtoull(l.substr(q, e - q).c_str(), nullptr, 10)); q = e + 1; } }
        else if (a == "--digest") g_digest = true;
        else if (a == "--watch") { unsigned lo, hi; std::sscanf(argv[++i], "%x:%x", &lo, &hi); g_watch.push_back({lo, hi, 0, false}); }
        else if (a == "--interp-hist") g_ihist = true;
        else if (a == "--no-ev") { g_no_ev = true; g_step_guard = ~0ull; }
        else if (a == "--input-replay") { std::ifstream f(argv[++i]); std::string ln; while (std::getline(f, ln)) { if (ln.empty() || ln[0] == '#') continue; unsigned long long fr; unsigned k; if (std::sscanf(ln.c_str(), "%llu,0x%x", &fr, &k) == 2) g_replay.push_back({fr, (uint16_t)k}); } }
        else if (a == "--capture-at") { std::string v = argv[++i]; auto c = v.find(':'); g_capture_at = std::strtoull(v.substr(0, c).c_str(), nullptr, 10); g_capture_file = v.substr(c + 1); }
        else if (a == "--island-keep") { g_island = true; g_island_keep = true; }
        else if (a == "--island-off") { g_island = true; g_island_off = true; }
        else if (a == "--lib-guard") { g_bus_watch = true; g_lib_guard = true; }
        else if (a == "--guard") { std::string v = argv[++i]; g_guard_on = v == "on" ? 1 : (v == "entry" ? 2 : (v == "event" ? 3 : 0)); }
        else if (a == "--skip-restore-invalidate") { g_skip_restore_invalidate = true; }
        else if (a == "--final-snap") { g_final_snap = true; }
        else if (a == "--ckpt2") { g_ckpt2 = argv[++i]; }
        else if (a == "--vblanks2") { g_vblanks2 = std::strtoull(argv[++i], nullptr, 10); }
        else if (a == "--inject-at-cycle") { unsigned long long v; char k[32]; unsigned ad = 0, vl = 0, ln = 1; std::sscanf(argv[++i], "%llu:%31[^:]:%x:%x:%u", &v, k, &ad, &vl, &ln); Inject in{0, k, ad, vl, ln, false}; in.at_retired = v; in.by_retired = true; in.by_cycle = true; g_injects.push_back(in); }
        else if (a == "--inject-at-retired") { unsigned long long v; char k[32]; unsigned ad = 0, vl = 0, ln = 1; std::sscanf(argv[++i], "%llu:%31[^:]:%x:%x:%u", &v, k, &ad, &vl, &ln); Inject in{0, k, ad, vl, ln, false}; in.at_retired = v; in.by_retired = true; g_injects.push_back(in); }
        else if (a == "--inject") { unsigned long long v; char k[32]; unsigned ad = 0, vl = 0, ln = 1; std::sscanf(argv[++i], "%llu:%31[^:]:%x:%x:%u", &v, k, &ad, &vl, &ln); g_injects.push_back({v, k, ad, vl, ln, false}); }
        else if (a == "--poke-at-retired") { unsigned long long v; unsigned ad, vl; std::sscanf(argv[++i], "%llu:%x:%x", &v, &ad, &vl); g_rpokes.push_back({v, ad, (uint8_t)vl, false}); }
        else if (a == "--poke") { unsigned long long v; unsigned ad, vl; std::sscanf(argv[++i], "%llu:%x:%x", &v, &ad, &vl); g_pokes.push_back({v, ad, (uint8_t)vl}); }
        else if (a == "--selftest-guard") { mode = "selftest"; }
        else if (a == "--make-altered") { mode = "altered"; altered_out = argv[++i]; }
        else if (a == "--lo") lo = std::strtoul(argv[++i], nullptr, 16);
        else if (a == "--hi") hi = std::strtoul(argv[++i], nullptr, 16);
    }
    { std::ifstream f(rom, std::ios::binary); std::vector<uint8_t> r((std::istreambuf_iterator<char>(f)), {});
      if (r.size() < 0x9DCDC + kGuardLen) { std::printf("rom load fail\n"); return 2; }
      g_frozen.assign(r.begin() + 0x9DCDC, r.begin() + 0x9DCDC + kGuardLen);
      std::printf("FROZEN_IMAGE rom_off=0x9DCDC len=%u fnv=%016llx\n", kGuardLen, (unsigned long long)fnv(kFnvOff, g_frozen.data(), g_frozen.size())); }
    if (mode == "selftest") return selftest_guard();
    if (mode == "altered") return make_altered(rom, bios, ck, altered_out);
    if (mode == "iwatch") return iwram_watch(rom, bios, ck, vbl, prefix, lo, hi);
    return trace_run(rom, bios, ck, mode, vbl, cap, prefix);
}
