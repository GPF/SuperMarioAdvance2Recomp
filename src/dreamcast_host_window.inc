// KOS implementation of GBARecomp's existing host-window boundary.
// Kept in SMA2 so the separate GBARecomp checkout remains untouched.
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/sound/stream.h>
#include <dc/video.h>
#if defined(SMA2_NORMAL_BENCH_PMU)
#include <dc/perfctr.h>
#endif
#include <kos/timer.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include "dreamcast_pvr_presenter.h"
#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
#include "dreamcast_native_pvr_bridge.h"
#endif
#if defined(SMA2_DEV_CHECKPOINT)
#include <string>
#include <vector>
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "runtime_arm.h"
#if defined(SMA2_IWRAM_AOT)
#include "sma2_iwram_aot.h"
#endif
#include "snapshots/sma2_checkpoint.h"
#if defined(SMA2_SNAPSHOT_H5_DIAG)
#include "snapshots/sma2_snapshot_h5_diag.h"
#endif
#endif

namespace gbarecomp {
namespace {
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kGameWidth = 240;
constexpr int kGameHeight = 160;
constexpr std::size_t kAudioRingSamples = 8192;
constexpr int kAudioStreamBufferBytes = 16 * 1024;

alignas(32) int16_t audio_ring[kAudioRingSamples]{};
alignas(32) int16_t audio_stream_buffer[kAudioStreamBufferBytes / sizeof(int16_t)]{};
uint64_t audio_head = 0;
uint64_t audio_tail = 0;
snd_stream_hnd_t audio_stream = SND_STREAM_INVALID;
uint64_t video_present_count = 0;
uint64_t video_vblank_wait_us = 0;
uint64_t video_convert_us = 0;

#if defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
// ── Normal-gameplay benchmark recorder (opt-in, SMA2_NORMAL_BENCH_FRAMES=N) ───────────────────────────────
// Observation only: one timestamp, two counters and one framebuffer hash per presented frame, and one final-state
// report at close(). No per-instruction work and no scheduling change. The framebuffer hash cost is accumulated
// separately (bench_hash_us) so it can be subtracted from per-frame wall time.
extern "C" uint64_t sma2_dc_time_us();
#if defined(SMA2_NORMAL_BENCH_PMU)
// One hardware event counted on PRFC1 over exactly the timed window (first present .. last present). PRFC0 is the
// KOS elapsed-time counter and is left alone. The event comes from /pc/sma2_pmu.txt (hex < 0x40), read at open(),
// before any restore or timed work. Counter reads bracket the diagnostic work that sits inside the window
// (framebuffer hash, vblank wait, RGB conversion) so it can be subtracted; nothing runs per guest instruction.
bool pmu_enabled = false, pmu_running = false;
unsigned pmu_event = 0;
uint64_t pmu_window = 0, pmu_hash = 0, pmu_wait = 0, pmu_convert = 0;
inline uint64_t pmu_now() { return pmu_running ? static_cast<uint64_t>(perf_cntr_count(PRFC1)) : 0; }
// Parses "13", "0x13" (optional surrounding whitespace). Returns 0 for anything else; valid events are 1..0x3f.
inline unsigned bench_pmu_parse(const char* s, size_t n) {
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
    unsigned v = 0; int digits = 0;
    for (; i < n; ++i, ++digits) {
        const char c = s[i]; unsigned d;
        if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else break;
        v = v << 4 | d;
        if (v > 0xffu) return 0;
    }
    return (digits > 0 && v >= 1u && v < 0x40u) ? v : 0u;
}
void bench_pmu_configure() {
    std::ifstream f("/pc/sma2_pmu.txt", std::ios::binary);
    char buf[16] = {0}; size_t n = 0;
    if (f) { f.read(buf, sizeof buf); n = static_cast<size_t>(f.gcount()); }
    pmu_event = bench_pmu_parse(buf, n);
    pmu_enabled = pmu_event != 0;
    std::fprintf(stderr, "SMA2_BENCH_PMU_CONFIG enabled=%d event=0x%02x\n", pmu_enabled ? 1 : 0, pmu_event);
}
#endif
#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)
extern "C" uint64_t sma2_ppu_compositor_total_us();
#endif
struct BenchFrame {
    uint64_t t_us, cycles, wait_us, convert_us, hash_us, fb_hash;
#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)
    uint64_t compositor_us;
#endif
    uint32_t pc;
};
constexpr int kBenchMaxFrames = 64;
BenchFrame bench_frames[kBenchMaxFrames];
int bench_frame_count = 0;
uint64_t bench_hash_total_us = 0;
uint64_t bench_fnv(const void* data, size_t bytes, uint64_t h = 14695981039346656037ull) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
void bench_on_present(const void* fb, size_t bytes) {
    if (bench_frame_count >= kBenchMaxFrames) return;
    const int idx = bench_frame_count;
    BenchFrame& f = bench_frames[bench_frame_count++];
    f.t_us = timer_us_gettime64();
#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)
    f.compositor_us = sma2_ppu_compositor_total_us();
#endif
#if defined(SMA2_NORMAL_BENCH_PMU)
    if (pmu_enabled && idx == 0) {                       // window start: immediately after the first timestamp
        perf_cntr_clear(PRFC1);
        perf_cntr_start(PRFC1, static_cast<perf_cntr_event_t>(pmu_event), PMCR_COUNT_CPU_CYCLES);
        pmu_running = true;
    }
    if (pmu_running && idx == SMA2_NORMAL_BENCH_FRAMES - 1) {   // window end: immediately after the last timestamp
        pmu_window = static_cast<uint64_t>(perf_cntr_count(PRFC1));
        perf_cntr_stop(PRFC1);
        pmu_running = false;
    }
#endif
    f.cycles = g_runtime_cycles;
    f.pc = g_cpu.R[15];
    f.wait_us = video_vblank_wait_us;
    f.convert_us = video_convert_us;
    const uint64_t h0 = timer_us_gettime64();
#if defined(SMA2_NORMAL_BENCH_PMU)
    const uint64_t p0 = pmu_now();
#endif
    f.fb_hash = bench_fnv(fb, bytes);
#if defined(SMA2_NORMAL_BENCH_PMU)
    if (pmu_running) pmu_hash += pmu_now() - p0;
#endif
    f.hash_us = timer_us_gettime64() - h0;
    bench_hash_total_us += f.hash_us;
}
void bench_report() {
    std::fprintf(stderr, "SMA2_BENCH_CONFIG frames_target=%d island=%d\n", SMA2_NORMAL_BENCH_FRAMES,
                 g_runtime_native_island);   // runtime value, not the compile-time option
    for (int i = 0; i < bench_frame_count; ++i) {
        const BenchFrame& f = bench_frames[i];
        const uint64_t dt = i ? f.t_us - bench_frames[i - 1].t_us : 0;
        const uint64_t dw = i ? f.wait_us - bench_frames[i - 1].wait_us : 0;
        const uint64_t dc = i ? f.convert_us - bench_frames[i - 1].convert_us : 0;
        const uint64_t dh = i ? bench_frames[i - 1].hash_us : 0;   // hash of the previous frame ran inside this interval
#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)
        const uint64_t dcomp = i ? f.compositor_us - bench_frames[i - 1].compositor_us : 0;
#endif
        std::fprintf(stderr,
#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)
            "SMA2_BENCH_FRAME i=%d t_us=%llu interval_us=%llu wait_us=%llu convert_us=%llu prev_hash_us=%llu "
            "sim_us=%lld compositor_total_us=%llu compositor_delta_us=%llu cycles=%llu pc=0x%08x fb=%016llx\n",
#else
            "SMA2_BENCH_FRAME i=%d t_us=%llu interval_us=%llu wait_us=%llu convert_us=%llu prev_hash_us=%llu "
            "sim_us=%lld cycles=%llu pc=0x%08x fb=%016llx\n",
#endif
            i, (unsigned long long)f.t_us, (unsigned long long)dt, (unsigned long long)dw, (unsigned long long)dc,
#if defined(SMA2_PPU_COMPOSITOR_COST_DIAG)
            (unsigned long long)dh, i ? (long long)dt - (long long)dw - (long long)dc - (long long)dh : 0,
            (unsigned long long)f.compositor_us, (unsigned long long)dcomp,
#else
            (unsigned long long)dh, i ? (long long)dt - (long long)dw - (long long)dc - (long long)dh : 0,
#endif
            (unsigned long long)f.cycles, f.pc, (unsigned long long)f.fb_hash);
    }
    if (bench_frame_count > 1)
        std::fprintf(stderr, "SMA2_BENCH_WINDOW first_to_last_us=%llu frames=%d hash_total_us=%llu\n",
                     (unsigned long long)(bench_frames[bench_frame_count - 1].t_us - bench_frames[0].t_us),
                     bench_frame_count, (unsigned long long)bench_hash_total_us);
    gba::GbaBus* bus = active_bus();
    if (bus) {
        std::fprintf(stderr, "SMA2_BENCH_FINAL pc=0x%08x cpsr=0x%08x cycles=%llu", g_cpu.R[15], (unsigned)g_cpu.cpsr,
                     (unsigned long long)g_runtime_cycles);
        for (int r = 0; r < 16; ++r) std::fprintf(stderr, " r%d=%08x", r, (unsigned)g_cpu.R[r]);
        std::fprintf(stderr,
            " iwram=%016llx ewram=%016llx pal=%016llx vram=%016llx oam=%016llx io=%016llx\n",
            (unsigned long long)bench_fnv(bus->iwram_read_ptr(), 32u * 1024u),
            (unsigned long long)bench_fnv(bus->ewram_ptr(), 256u * 1024u),
            (unsigned long long)bench_fnv(bus->pal_ptr(), 1024u),
            (unsigned long long)bench_fnv(bus->vram_ptr(), 96u * 1024u),
            (unsigned long long)bench_fnv(bus->oam_ptr(), 1024u),
            (unsigned long long)bench_fnv(bus->io().raw(), gba::GbaIo::kIoSize));
    }
#if defined(SMA2_NORMAL_BENCH_PMU)
    if (pmu_enabled)
        std::fprintf(stderr,
            "SMA2_BENCH_PMU event=0x%02x window=%llu hash=%llu vblank_wait=%llu convert=%llu simulation=%lld\n",
            pmu_event, (unsigned long long)pmu_window, (unsigned long long)pmu_hash, (unsigned long long)pmu_wait,
            (unsigned long long)pmu_convert,
            (long long)pmu_window - (long long)pmu_hash - (long long)pmu_wait - (long long)pmu_convert);
#endif
#if defined(SMA2_IWRAM_AOT)
    sma2::iwram_aot_report("bench_end");
#endif
}
#endif
#if defined(SMA2_PVR_PRESENT)
DreamcastPvrPresenter g_pvr_presenter;
#endif
uint64_t audio_submit_us = 0;
uint64_t audio_queued_samples = 0;
std::atomic<uint32_t> audio_consumed_samples{0};
std::atomic<uint32_t> audio_silence_samples{0};

#if defined(SMA2_DEV_CHECKPOINT)
constexpr char kSma2RomSha1[] = "5101ddf223d1d918928fe1f306b63a42ada14a5e";
constexpr char kSma2BiosSha1[] = "300c20df6731a33952ded8c436f7f186d25d3492";
constexpr char kCheckpointPath[] = "/pc/sma2_checkpoint.bin";
bool dev_y_was_pressed = false;

uint32_t maple_buttons() {
#if defined(SMA2_SNAPSHOT_H5_DIAG)
#if defined(SMA2_DEV_CHECKPOINT_AUTOLOAD)
    return 0;
#else
    static unsigned sample = 0;
    return sample++ == 1 ? CONT_Y : 0;
#endif
#elif defined(SMA2_SNAPSHOT_INPUT_DIAG)
    static unsigned sample = 0;
    const unsigned current = sample++;
    const bool y_down = (current >= 1 && current <= 3) || current == 5;
    const uint32_t buttons = y_down ? CONT_Y : 0;
    if (current < 6)
        std::fprintf(stderr,
                     "[snapshot-input-diag] sample=%u synthetic_buttons=0x%08x\n",
                     current, static_cast<unsigned>(buttons));
    return buttons;
#else
    maple_device_t* device = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!device) return 0;
    const auto* state = static_cast<const cont_state_t*>(maple_dev_status(device));
    return state ? state->buttons : 0;
#endif
}

bool dev_save_trigger() {
#if defined(SMA2_MEMORY_ALLOC_DIAG)
    // The allocator probe must never enter checkpoint capture.
    return false;
#else
    const uint32_t buttons = maple_buttons();
    const bool y_pressed = (buttons & CONT_Y) != 0;
    constexpr uint32_t kExitCombo =
        CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y;
    const bool exiting = (buttons & kExitCombo) == kExitCombo;
    const bool trigger = y_pressed && !dev_y_was_pressed && !exiting;
    dev_y_was_pressed = y_pressed;
    return trigger;
#endif
}

void dev_save_checkpoint() {
    gba::GbaBus* bus = active_bus();
    gba::GbaPpu* ppu = active_ppu();
    if (!bus || !ppu) {
        std::fprintf(stderr, "[dreamcast-checkpoint] no active machine\n");
        return;
    }
    sma2::CheckpointContext ctx;
    ctx.bus = bus;
    ctx.ppu = ppu;
    ctx.rom_sha1 = kSma2RomSha1;
    ctx.bios_sha1 = kSma2BiosSha1;

    std::string err;
    sma2::BoundaryInfo info;
    if (!sma2::at_safe_boundary(ctx, &info, &err)) {
        std::fprintf(stderr, "[dreamcast-checkpoint] boundary refused: %s\n",
                     err.c_str());
        return;
    }
#if defined(SMA2_SNAPSHOT_INPUT_DIAG)
    std::fprintf(stderr,
                 "[snapshot-input-diag] safe-boundary pc=0x%08x frame=%llu "
                 "cycles=%llu irq=%u iret=%u returns=%u/%u tag=%u\n",
                 info.resume_pc,
                 static_cast<unsigned long long>(info.frame),
                 static_cast<unsigned long long>(info.cycles), info.irq_depth,
                 info.iret_depth, info.return_depth, info.return_floor, info.tag);
#endif
    std::vector<uint8_t> blob;
    const char* checkpoint_name = "DEV";
#if defined(SMA2_SNAPSHOT_H5_DIAG)
    checkpoint_name = "H5";
#endif
    if (!sma2::capture_checkpoint(&blob, ctx, checkpoint_name, info, &err)) {
        std::fprintf(stderr, "[dreamcast-checkpoint] capture failed: %s\n",
                     err.c_str());
        return;
    }
    if (!sma2::write_checkpoint_file(kCheckpointPath, blob, &err)) {
        std::fprintf(stderr, "[dreamcast-checkpoint] write failed: %s\n",
                     err.c_str());
        return;
    }
#if defined(SMA2_SNAPSHOT_H5_DIAG)
    sma2::h5_diag::boundary_saved(blob, ctx);
#endif
    std::printf("[dreamcast-checkpoint] saved %zu bytes frame=%llu\n",
                blob.size(), static_cast<unsigned long long>(info.frame));
    std::fflush(stdout);
    std::fflush(stderr);
}
#endif

void* audio_stream_callback(snd_stream_hnd_t, int requested_bytes,
                            int* received_bytes) {
    if (!received_bytes) return nullptr;
    if (requested_bytes < 0) requested_bytes = 0;
    if (requested_bytes > kAudioStreamBufferBytes)
        requested_bytes = kAudioStreamBufferBytes;
    requested_bytes &= ~3; // KOS PCM16 stream transfers are 4-byte aligned.
    const std::size_t requested_samples =
        static_cast<std::size_t>(requested_bytes) / sizeof(int16_t);
    uint32_t consumed_samples = 0;
    for (std::size_t i = 0; i < requested_samples; ++i) {
        if (audio_tail != audio_head) {
            audio_stream_buffer[i] = audio_ring[audio_tail & (kAudioRingSamples - 1)];
            ++audio_tail;
            ++consumed_samples;
        } else {
            audio_stream_buffer[i] = 0;
        }
    }
    audio_consumed_samples.fetch_add(consumed_samples, std::memory_order_relaxed);
    audio_silence_samples.fetch_add(
        static_cast<uint32_t>(requested_samples) - consumed_samples,
        std::memory_order_relaxed);
    *received_bytes = requested_bytes;
    return audio_stream_buffer;
}

bool start_audio_stream() {
    if (audio_stream != SND_STREAM_INVALID) return true;
    audio_head = audio_tail = 0;
    if (snd_stream_init() < 0) {
        std::fprintf(stderr, "[dreamcast-audio] snd_stream_init failed\n");
        return false;
    }
    audio_stream = snd_stream_alloc(audio_stream_callback,
                                    kAudioStreamBufferBytes);
    if (audio_stream == SND_STREAM_INVALID) {
        snd_stream_shutdown();
        std::fprintf(stderr, "[dreamcast-audio] snd_stream_alloc failed\n");
        return false;
    }
    snd_stream_start(audio_stream, 32768, 0); // GBA mono PCM at 32 kHz.
    std::fprintf(stderr, "[dreamcast-audio] KOS mono PCM stream started at 32768 Hz\n");
    return true;
}

void stop_audio_stream() {
    if (audio_stream == SND_STREAM_INVALID) return;
    snd_stream_stop(audio_stream);
    snd_stream_destroy(audio_stream);
    audio_stream = SND_STREAM_INVALID;
    snd_stream_shutdown();
}

uint16_t gba_keyinput() {
#if defined(SMA2_SNAPSHOT_H5_DIAG)
    // Both runs expose released guest keys. Synthetic Y is consumed only by
    // the developer save trigger and never reaches the GBA KEYINPUT register.
    return 0x03ff;
#endif
    uint16_t keys = 0x03ff;
    maple_device_t* device = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!device) return keys;
    const auto* state = static_cast<const cont_state_t*>(maple_dev_status(device));
    if (!state) return keys;
    const uint32_t b = state->buttons;
    const bool right_trigger_down = state->rtrig >= 0x80;
    if ((b & CONT_A) || right_trigger_down) keys &= ~(1u << 0);
    if (b & CONT_B) keys &= ~(1u << 1);
    if (b & CONT_D) keys &= ~(1u << 2); // Select
    if (b & CONT_START) keys &= ~(1u << 3);
    if (b & CONT_DPAD_RIGHT) keys &= ~(1u << 4);
    if (b & CONT_DPAD_LEFT) keys &= ~(1u << 5);
    if (b & CONT_DPAD_UP) keys &= ~(1u << 6);
    if (b & CONT_DPAD_DOWN) keys &= ~(1u << 7);
    if (b & CONT_Y) keys &= ~(1u << 8); // GBA R
    if (b & CONT_X) keys &= ~(1u << 9); // GBA L
    static bool have_previous = false;
    static bool previous_right_trigger_down = false;
    if (!have_previous ||
        right_trigger_down != previous_right_trigger_down) {
        std::fprintf(stderr,
                     "[dreamcast-input] maple_buttons=0x%08x rtrig=%u keyinput=0x%04x\n",
                     static_cast<unsigned>(b),
                     static_cast<unsigned>(state->rtrig),
                     static_cast<unsigned>(keys));
        previous_right_trigger_down = right_trigger_down;
        have_previous = true;
    }
    return keys;
}

#if defined(SMA2_DEV_CHECKPOINT_AUTOLOAD)
__attribute__((noinline)) bool startup_autoload() {
    gba::GbaBus* bus = active_bus();
    gba::GbaPpu* ppu = active_ppu();
    if (!bus || !ppu) {
        std::fprintf(stderr, "CHECKPOINT_RESTORE_FAILED no active machine\n");
        std::fflush(stderr);
        return false;
    }
    sma2::CheckpointContext ctx;
    ctx.bus = bus;
    ctx.ppu = ppu;
    ctx.rom_sha1 = kSma2RomSha1;
    ctx.bios_sha1 = kSma2BiosSha1;
    const sma2::AutoloadResult result =
        sma2::autoload_checkpoint(ctx, kCheckpointPath);
    if (result.failure == sma2::CheckpointFailure::NotFound) {
        std::printf("CHECKPOINT_NOT_FOUND %s; continuing normal interpreter startup\n",
                    result.detail.c_str());
        std::fflush(stdout);
        return true;
    }
    if (result.failure != sma2::CheckpointFailure::None) {
        std::fprintf(stderr, "%s %s%s\n",
                     sma2::checkpoint_failure_label(result.failure),
                     result.detail.c_str(),
                     result.mutation_started ? " (state validity uncertain)" : "");
        std::fflush(stderr);
        return false;
    }
#if defined(SMA2_SNAPSHOT_H5_DIAG)
    std::string h5_err;
    if (!sma2::h5_diag::compare_immediate_restore(ctx, kCheckpointPath,
                                                  &h5_err)) {
        std::fprintf(stderr, "SNAPSHOT_H5_IMMEDIATE_FAIL %s\n", h5_err.c_str());
        std::fflush(stderr);
        return false;
    }
#endif
    std::printf("CHECKPOINT_OK restored frame=%llu\n",
                static_cast<unsigned long long>(result.boundary_frame));
    std::fflush(stdout);
    return true;
}
#endif
} // namespace

#if defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
extern "C" void sma2_normal_bench_report_now() {
    bench_report();
}
#endif

HostWindow::HostWindow() = default;
HostWindow::~HostWindow() { close(); }
bool HostWindow::is_available() { return true; }

bool HostWindow::open(int, int, int, const char*, const char*, bool, bool,
                      bool, bool) {
    vid_set_mode(DM_320x240, PM_RGB565);
#if defined(SMA2_PVR_PRESENT) || defined(SMA2_NATIVE_PVR_GBA_RENDER)
    vid_set_dithering(false);
#endif
    vid_border_color(0, 0, 0);
#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
    if (!native_pvr_bridge_open()) {
        std::fprintf(stderr, "[dreamcast-native-pvr] initialization failed; refusing mixed presentation ownership\n");
        return false;
    }
#endif
#if defined(SMA2_PVR_PRESENT)
    if (!g_pvr_presenter.open()) {
        if (!g_pvr_presenter.cpu_fallback_safe()) {
            std::fprintf(stderr,
                         "[dreamcast-pvr] init failed without safe ownership recovery\n");
            return false;
        }
        std::fprintf(stderr, "[dreamcast-pvr] init failed; using CPU presenter\n");
    }
#endif
#if defined(SMA2_DEV_CHECKPOINT)
    // SMA2 has no cartridge RTC signature, but the portable device codec
    // requires an emulated clock in every serialized RTC object. Seed only
    // the inactive device; it is not guest-visible for this ROM.
    if (gba::GbaBus* bus = active_bus(); bus && !bus->rtc().active()) {
        bus->rtc().set_emulated_clock(0);
        std::fprintf(stderr,
                     "[dreamcast-checkpoint] inactive RTC seeded for snapshot codec\n");
    }
#endif
#if defined(SMA2_NORMAL_BENCH_PMU)
    bench_pmu_configure();
#endif
#if defined(SMA2_IWRAM_AOT)
    // Arm the IWRAM code guard BEFORE any checkpoint restore: it starts invalid, every restore/bulk writer
    // invalidates it, and the first execution inside the mixer range proves the live image with one compare.
    if (gba::GbaBus* bus = active_bus(); !bus || !sma2::iwram_aot_install(*bus)) {
        std::fprintf(stderr, "SMA2_IWRAM_AOT_REFUSED guard could not be installed; refusing relocated IWRAM dispatch\n");
        return false;
    }
#endif
#if defined(SMA2_DEV_CHECKPOINT_AUTOLOAD)
    if (!startup_autoload()) return false;
#endif
    open_ = true;
    if (!start_audio_stream())
        std::fprintf(stderr, "[dreamcast-audio] continuing without audio\n");
    std::printf("dreamcast_video=320x240 RGB565, centered GBA 240x160\n");
    return true;
}

void HostWindow::close() {
#if defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
    bench_report();
#endif
#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
    native_pvr_bridge_close();
#endif
#if defined(SMA2_PVR_PRESENT)
    g_pvr_presenter.close();
#endif
    stop_audio_stream();
    open_ = false;
}
void HostWindow::set_title(const char*) {}
bool HostWindow::set_surface_size(int, int) { return true; }
bool HostWindow::drawable_size(int* w, int* h) const {
    if (w) *w = kScreenWidth;
    if (h) *h = kScreenHeight;
    return true;
}

void HostWindow::present(const uint8_t* rgb) {
    if (!open_ || !rgb || !vram_s) return;
    ++video_present_count;
#if defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
    bench_on_present(rgb, static_cast<size_t>(kGameWidth) * kGameHeight * 3u);
#endif
#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
    if (native_pvr_bridge_owns_presentation()) return;
#endif
    constexpr int x0 = (kScreenWidth - kGameWidth) / 2;
    constexpr int y0 = (kScreenHeight - kGameHeight) / 2;
    const uint64_t wait_start = timer_us_gettime64();
#if defined(SMA2_NORMAL_BENCH_PMU)
    const uint64_t pmu_w0 = pmu_now();
#endif
    vid_waitvbl();
#if defined(SMA2_NORMAL_BENCH_PMU)
    if (pmu_running) pmu_wait += pmu_now() - pmu_w0;
#endif
    video_vblank_wait_us += timer_us_gettime64() - wait_start;
#if defined(SMA2_PVR_PRESENT)
    if (pvr_present::select_owner(true, g_pvr_presenter.active()) ==
        pvr_present::Owner::Pvr) {
        if (g_pvr_presenter.present(rgb)) return;
        std::fprintf(stderr,
                     "[dreamcast-pvr] disabled after recovery; CPU presenter resumed\n");
        if (!g_pvr_presenter.cpu_fallback_safe()) {
            std::fprintf(stderr,
                         "[dreamcast-pvr] shutdown failed; refusing concurrent CPU output\n");
            return;
        }
    }
#endif
    const uint64_t convert_start = timer_us_gettime64();
#if defined(SMA2_NORMAL_BENCH_PMU)
    const uint64_t pmu_c0 = pmu_now();
#endif
    for (int y = 0; y < kGameHeight; ++y) {
        uint16_t* dst = vram_s + (y + y0) * kScreenWidth + x0;
        const uint8_t* src = rgb + y * kGameWidth * 3;
        for (int x = 0; x < kGameWidth; ++x) {
            const uint8_t r = src[x * 3 + 0];
            const uint8_t g = src[x * 3 + 1];
            const uint8_t b = src[x * 3 + 2];
            dst[x] = static_cast<uint16_t>(((r >> 3) << 11) |
                                           ((g >> 2) << 5) | (b >> 3));
        }
    }
#if defined(SMA2_NORMAL_BENCH_PMU)
    if (pmu_running) pmu_convert += pmu_now() - pmu_c0;
#endif
    video_convert_us += timer_us_gettime64() - convert_start;
}

void HostWindow::present(const uint8_t* rgb, unsigned width, unsigned height) {
    // GBARecomp's shared runtime uses this overload for sampled/high-resolution
    // frames. SMA2's Dreamcast presenter has a fixed native-size RGB888 path;
    // only forward the dimensions it can consume without changing pixel layout.
    if (width != static_cast<unsigned>(kGameWidth) ||
        height != static_cast<unsigned>(kGameHeight)) {
        std::fprintf(stderr,
                     "[dreamcast-video] unsupported sampled frame %ux%u; "
                     "expected %ux%u\n",
                     width, height, kGameWidth, kGameHeight);
        return;
    }
    present(rgb);
}

#if defined(SMA2_RGB565_SIDECAR)
bool HostWindow::present_rgb565(const uint16_t* rgb565) {
    if (!open_ || !rgb565 || !vram_s) return false;
    ++video_present_count;
    const uint64_t wait_start = timer_us_gettime64();
    vid_waitvbl();
    video_vblank_wait_us += timer_us_gettime64() - wait_start;
#if defined(SMA2_PVR_PRESENT)
    if (pvr_present::select_owner(true, g_pvr_presenter.active()) ==
        pvr_present::Owner::Pvr) {
        if (g_pvr_presenter.present(rgb565)) return true;
        std::fprintf(stderr,
                     "[dreamcast-pvr] disabled after recovery; RGB565 CPU presenter resumed\n");
        if (!g_pvr_presenter.cpu_fallback_safe()) {
            std::fprintf(stderr,
                         "[dreamcast-pvr] shutdown failed; refusing concurrent CPU output\n");
            return true;  // Frame handled by deliberate drop; never mix owners.
        }
    }
#endif
    pvr_present::copy_rgb565_to_scanout(rgb565, vram_s, kScreenWidth,
                                        kScreenHeight);
    return true;
}
#endif

void HostWindow::load_input_config(const char*, bool, int) {}
void HostWindow::set_fullscreen(int) {}
int HostWindow::fullscreen() const { return 0; }
void HostWindow::adjust_scale(int) {}
void HostWindow::set_volume(int) {}
int HostWindow::volume() const { return 100; }
int HostWindow::window_scale() const { return 1; }
void HostWindow::set_linear_filter(bool) {}
bool HostWindow::linear_filter() const { return false; }
void HostWindow::set_audio_enabled(bool enabled) {
    if (enabled) start_audio_stream();
    else stop_audio_stream();
}
bool HostWindow::audio_enabled() const {
    return audio_stream != SND_STREAM_INVALID;
}
void HostWindow::set_resize_driven_view(bool) {}
#if defined(GBARECOMP_RUNTIME_UI)
void HostWindow::set_runtime_ui(RecompRuntimeUi*) {}
#endif
void HostWindow::configure_touch(bool, std::uint32_t, int, bool) {}
void HostWindow::set_touch_config_dir(const char*) {}
bool HostWindow::touch_pad_visible() const { return false; }
void HostWindow::set_touch_pad_visible(bool) {}
void HostWindow::set_view_margins(std::uint32_t, std::uint32_t,
                                  std::uint32_t, std::uint32_t) {}
void HostWindow::set_host_overlay(void (*)(HostOverlay*)) {}
float HostWindow::px_per_mm() const { return 1.0f; }
void HostWindow::set_presentation_anchor_top(bool) {}
bool HostWindow::wait_for_foreground() { return true; }
void HostWindow::haptic_pulse(int, float) {}
void HostWindow::set_fps_readout(bool) {}
bool HostWindow::fps_readout() const { return false; }
bool HostWindow::assist_tools_enabled() const { return false; }
int HostWindow::fast_forward_multiplier() const { return 1; }
void HostWindow::push_audio_samples(const int16_t* samples, std::size_t count) {
    if (!samples || audio_stream == SND_STREAM_INVALID || count == 0) return;
    const uint64_t submit_start = timer_us_gettime64();
    if (count > kAudioRingSamples) {
        samples += count - kAudioRingSamples;
        count = kAudioRingSamples;
    }
    const uint64_t used = audio_head - audio_tail;
    if (used + count > kAudioRingSamples)
        audio_tail = audio_head + count - kAudioRingSamples;
    for (std::size_t i = 0; i < count; ++i) {
        audio_ring[audio_head & (kAudioRingSamples - 1)] = samples[i];
        ++audio_head;
    }
    audio_queued_samples += count;
    // The stream callback copies into its aligned buffer synchronously here.
    snd_stream_poll(audio_stream);
    audio_submit_us += timer_us_gettime64() - submit_start;
}

extern "C" void sma2_dc_host_perf_snapshot(
    uint64_t* presents, uint64_t* vblank_wait, uint64_t* video_convert,
    uint64_t* audio_submit, uint64_t* audio_queued, uint32_t* audio_consumed,
    uint32_t* audio_silence) {
    if (presents) *presents = video_present_count;
    if (vblank_wait) *vblank_wait = video_vblank_wait_us;
    if (video_convert) *video_convert = video_convert_us;
    if (audio_submit) *audio_submit = audio_submit_us;
    if (audio_queued) *audio_queued = audio_queued_samples;
    if (audio_consumed)
        *audio_consumed = audio_consumed_samples.load(std::memory_order_relaxed);
    if (audio_silence)
        *audio_silence = audio_silence_samples.load(std::memory_order_relaxed);
}

extern "C" uint64_t sma2_dc_time_us() { return timer_us_gettime64(); }
void host_haptic_pulse(int, float) {}
void host_request_settings_menu() {}
void HostWindow::service_events() {}

HostWindow::Events HostWindow::pump() {
    Events events{};
    events.quit = false;
    events.keyinput = gba_keyinput();
#if defined(SMA2_DEV_CHECKPOINT)
    if (dev_save_trigger()) dev_save_checkpoint();
#endif
    return events;
}
} // namespace gbarecomp
