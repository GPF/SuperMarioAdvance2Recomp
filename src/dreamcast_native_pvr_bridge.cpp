#include "dreamcast_native_pvr_bridge.h"

#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
#include <dc/pvr.h>
#include <dc/video.h>
#include <kos/timer.h>
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>
#include "dc-pvr-gba.h"
#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime_bus_bridge.h"
#include "dreamcast_native_pvr_bridge_contract.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(SMA2_NATIVE_PVR_FRONT_CAPTURE) || \
    defined(SMA2_NATIVE_PVR_DIAG_STOP_FRAMES)
#include <cstdlib>
#endif

namespace gbarecomp {
#if defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
extern "C" void sma2_normal_bench_report_now();
#endif
namespace {
constexpr unsigned kWidth = 240;
constexpr unsigned kHeight = 160;
constexpr std::size_t kPixels = kWidth * kHeight;
#if defined(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
constexpr unsigned kMixedFallbackRows = 8;
#endif
struct mCore* shadowCore;
struct GBA* shadowGba;
std::array<color_t, kPixels> fallbackBuffer{};
std::array<bool, kHeight> nativeRows{};
std::array<bool, kHeight> seenRows{};
std::array<uint8_t, kHeight> fallbackReasons{};
bool pvrReady;
bool pvrFailed;
uint64_t framesSubmitted;
uint64_t syncBytes;
uint64_t syncWrites;
uint64_t lineStateHash = 1469598103934665603ull;
uint64_t writeHistoryHash = 1469598103934665603ull;

#if defined(SMA2_NATIVE_PVR_COST_DIAG)
struct BridgeCostFrame {
    uint64_t guestFrame{};
    uint64_t lineUs{};
    uint64_t registerUs{};
    uint64_t drawUs{};
    uint64_t busMirrorUs{};
    uint64_t conversionUs{};
    uint64_t prePvrUs{};
    uint64_t uploadUs{};
    uint64_t submitUs{};
    uint32_t rows{};
    uint32_t nativeRows{};
    uint32_t fallbackRows{};
    uint32_t capturedRows{};
    uint32_t displayedRows{};
    std::array<uint32_t, 7> fallbackReasons{};
    uint64_t syncBytes{};
    uint64_t syncWrites{};
};
constexpr unsigned kCostFrames = 40;
std::array<BridgeCostFrame, kCostFrames> bridgeCosts{};
unsigned bridgeCostCount{};
BridgeCostFrame activeBridgeCost{};
bool bridgeCostReported{};
#endif

#if defined(SMA2_NATIVE_PVR_FRONT_CAPTURE)
#ifndef SMA2_NATIVE_PVR_CAPTURE_FRAMES
#define SMA2_NATIVE_PVR_CAPTURE_FRAMES 1
#endif
#ifndef SMA2_NATIVE_PVR_CAPTURE_START_FRAME
#define SMA2_NATIVE_PVR_CAPTURE_START_FRAME 201
#endif
constexpr uint64_t kCaptureStartFrame = SMA2_NATIVE_PVR_CAPTURE_START_FRAME;
constexpr uint64_t kCaptureFrameCount = SMA2_NATIVE_PVR_CAPTURE_FRAMES;
constexpr uint64_t kCaptureLastFrame = kCaptureStartFrame + kCaptureFrameCount - 1;
constexpr unsigned kCaptureScreenWidth = 320;
constexpr unsigned kCaptureScreenHeight = 240;
constexpr unsigned kCaptureCropX = 40;
constexpr unsigned kCaptureCropY = 40;
constexpr std::size_t kCaptureScreenBytes =
    kCaptureScreenWidth * kCaptureScreenHeight * sizeof(uint16_t);
constexpr std::size_t kCaptureOutputBytes = kPixels * sizeof(uint16_t);
std::array<uint16_t, kCaptureScreenWidth * kCaptureScreenHeight> frontSnapshot{};
std::array<uint16_t, kPixels> capturedVisible{};
std::array<uint16_t, kPixels> softwareOracle565{};

bool write_exact(const char* path, const void* data, std::size_t size) {
    FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    bool ok = std::fwrite(data, 1, size, file) == size;
    if (std::fflush(file) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    return ok;
}

uint64_t hash_words(const uint16_t* pixels, std::size_t count) {
    uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < count; ++i) {
        hash = (hash ^ static_cast<uint8_t>(pixels[i])) * 1099511628211ull;
        hash = (hash ^ static_cast<uint8_t>(pixels[i] >> 8)) * 1099511628211ull;
    }
    return hash;
}

bool capture_gameplay_front(const uint8_t* rgb, uint64_t frame,
                            uint64_t stateHash, uint64_t rgb888Hash,
                            uint64_t rgb565Hash, unsigned rows,
                            unsigned native, unsigned fallback,
                            uint64_t uploadUs, uint64_t submitUs) {
    const uintptr_t pvrBase = (uintptr_t) PVR_RAM_BASE;
    pvr_ptr_t frontBefore, backBefore, frontAfter, backAfter;
    const uint16_t* mappedFront;
    uint64_t waitStart, captureUs;
    int readyResult, doneResult;
    char oraclePath[96], pvrPath[96];
    unsigned mismatchCount = 0;
    int firstX = -1, firstY = -1;

    if (!vid_mode || vid_mode->pm != PM_RGB565 ||
        vid_mode->width != kCaptureScreenWidth ||
        vid_mode->height != kCaptureScreenHeight) {
        std::printf("SMA2_NATIVE_PVR_CAPTURE_FAIL reason=video_mode\n");
        return false;
    }
    for (std::size_t i = 0; i < kPixels; ++i) {
        const uint8_t r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        softwareOracle565[i] = static_cast<uint16_t>(((r >> 3) << 11) |
                                                     ((g >> 2) << 5) | (b >> 3));
    }
    std::snprintf(oraclePath, sizeof(oraclePath),
                  "/pc/sma2_gameplay_frame%llu_software.rgb565",
                  static_cast<unsigned long long>(frame));
    std::snprintf(pvrPath, sizeof(pvrPath),
                  "/pc/sma2_gameplay_frame%llu_pvr.rgb565",
                  static_cast<unsigned long long>(frame));
    if (!write_exact(oraclePath,
                     softwareOracle565.data(), kCaptureOutputBytes)) {
        std::printf("SMA2_NATIVE_PVR_CAPTURE_FAIL reason=oracle_write\n");
        return false;
    }

    waitStart = timer_us_gettime64();
    readyResult = pvr_wait_ready();
    doneResult = readyResult < 0 ? -1 : pvr_wait_render_done();
    if (readyResult < 0 || doneResult < 0) {
        std::printf("SMA2_NATIVE_PVR_CAPTURE_FAIL reason=render_wait ready=%d done=%d\n",
                    readyResult, doneResult);
        return false;
    }
    vid_waitvbl();
    frontBefore = pvr_get_front_buffer();
    backBefore = pvr_get_back_buffer();
    const uintptr_t frontAddress = reinterpret_cast<uintptr_t>(frontBefore);
    const uintptr_t backAddress = reinterpret_cast<uintptr_t>(backBefore);
    if (frontAddress < pvrBase || backAddress < pvrBase ||
        frontAddress == backAddress || ((frontAddress - pvrBase) & 1u) ||
        ((backAddress - pvrBase) & 1u) ||
        (frontAddress - pvrBase) / 2 + kCaptureScreenBytes > PVR_RAM_SIZE ||
        (backAddress - pvrBase) / 2 + kCaptureScreenBytes > PVR_RAM_SIZE) {
        std::printf("SMA2_NATIVE_PVR_CAPTURE_FAIL reason=page_range front=%p back=%p\n",
                    reinterpret_cast<void*>(frontBefore),
                    reinterpret_cast<void*>(backBefore));
        return false;
    }
    mappedFront = reinterpret_cast<const uint16_t*>(
        pvrBase + (frontAddress - pvrBase) / 2);
    std::memcpy(frontSnapshot.data(), mappedFront, kCaptureScreenBytes);
    frontAfter = pvr_get_front_buffer();
    backAfter = pvr_get_back_buffer();
    if (frontBefore != frontAfter || backBefore != backAfter) {
        std::printf("SMA2_NATIVE_PVR_CAPTURE_FAIL reason=page_changed front_before=%p front_after=%p back_before=%p back_after=%p\n",
                    reinterpret_cast<void*>(frontBefore),
                    reinterpret_cast<void*>(frontAfter),
                    reinterpret_cast<void*>(backBefore),
                    reinterpret_cast<void*>(backAfter));
        return false;
    }
    for (unsigned y = 0; y < kHeight; ++y) {
        for (unsigned x = 0; x < kWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * kWidth + x;
            capturedVisible[i] = frontSnapshot[
                static_cast<std::size_t>(y + kCaptureCropY) * kCaptureScreenWidth +
                x + kCaptureCropX];
            if (capturedVisible[i] != softwareOracle565[i]) {
                if (!mismatchCount) { firstX = static_cast<int>(x); firstY = static_cast<int>(y); }
                ++mismatchCount;
            }
        }
    }
    if (!write_exact(pvrPath,
                     capturedVisible.data(), kCaptureOutputBytes)) {
        std::printf("SMA2_NATIVE_PVR_CAPTURE_FAIL reason=capture_write\n");
        return false;
    }
    captureUs = timer_us_gettime64() - waitStart;
    std::printf("SMA2_NATIVE_PVR_CAPTURE frame=%llu pixels=%u mismatches=%u first_x=%d first_y=%d software_rgb888_fnv=%016llx software_rgb565_fnv=%016llx pvr_rgb565_fnv=%016llx state_fnv=%016llx rows=%u native=%u fallback=%u ready=%d render_done=%d vblank=1 front=%p back=%p front_after=%p back_after=%p stable=1 capture_us=%llu upload_us=%llu submit_us=%llu oracle=%s\n",
                static_cast<unsigned long long>(frame), static_cast<unsigned>(kPixels),
                mismatchCount, firstX, firstY,
                static_cast<unsigned long long>(rgb888Hash),
                static_cast<unsigned long long>(rgb565Hash),
                static_cast<unsigned long long>(hash_words(capturedVisible.data(), kPixels)),
                static_cast<unsigned long long>(stateHash), rows, native, fallback,
                readyResult, doneResult, reinterpret_cast<void*>(frontBefore),
                reinterpret_cast<void*>(backBefore), reinterpret_cast<void*>(frontAfter),
                reinterpret_cast<void*>(backAfter),
                static_cast<unsigned long long>(captureUs),
                static_cast<unsigned long long>(uploadUs),
                static_cast<unsigned long long>(submitUs),
                mismatchCount ? "FAIL" : "PASS");
    std::fflush(stdout);
    return mismatchCount == 0;
}
#endif

class Bridge final : public gba::GbaPpu::PresentationObserver,
                     public gba::BusWriteObserver {
public:
    static void set_instance(Bridge* instance) { bridge_ = instance; }

    bool open() {
#if defined(SMA2_NATIVE_PVR_SKIP_SOFTWARE)
        skipSoftware_ = true;
#endif
        ppu_ = active_ppu();
        bus_ = active_bus();
        if (!ppu_ || !bus_) return false;
        previousPresentation_ = ppu_->presentation_observer();
        previousWrite_ = bus_->exchange_write_observer(this);
        ppu_->set_presentation_observer(this);

        static const uint8_t dummyRom[256]{};
        shadowCore = GBACoreCreate();
        if (!shadowCore || !shadowCore->init(shadowCore)) return fail("core_init");
        mCoreInitConfig(shadowCore, nullptr);
        shadowCore->setVideoBuffer(shadowCore, fallbackBuffer.data(), kWidth);
        if (!shadowCore->loadROM(shadowCore,
                                 VFileFromConstMemory(dummyRom, sizeof(dummyRom))))
            return fail("rom_load");
        shadowCore->reset(shadowCore); // Reset state only; never execute the shadow CPU.
        shadowGba = static_cast<GBA*>(shadowCore->board);
        if (pvr_init(&PVRGBAInitParams) < 0 || !PVRGBAInit(shadowGba))
            return fail("pvr_init");
        sma2_native_pvr_set_line_result_hook(line_hook);
        pvrReady = true;
        reseed();
#if defined(SMA2_NATIVE_PVR_SKIP_SOFTWARE)
        constexpr const char* mode = "native-skip";
#else
        constexpr const char* mode = "validation";
#endif
        std::printf("SMA2_NATIVE_PVR_INIT shadow=%p gba=%p renderer=%p mode=%s shadow_cpu_steps=0\n",
                    static_cast<void*>(shadowCore), static_cast<void*>(shadowGba),
                    static_cast<void*>(shadowGba->video.renderer), mode);
        return true;
    }

    void close() {
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        if (!bridgeCostReported) {
            for (unsigned i = 0; i < bridgeCostCount; ++i) {
                const BridgeCostFrame& c = bridgeCosts[i];
                std::printf("SMA2_NATIVE_PVR_COST frame=%llu rows=%u native=%u fallback=%u reasons=%u,%u,%u,%u,%u,%u,%u captured=%u displayed=%u line_us=%llu reg_us=%llu draw_us=%llu bus_mirror_us=%llu convert_us=%llu pre_pvr_us=%llu upload_us=%llu submit_us=%llu sync_bytes=%llu sync_writes=%llu\n",
                        static_cast<unsigned long long>(c.guestFrame),
                        static_cast<unsigned>(c.rows),
                        static_cast<unsigned>(c.nativeRows),
                        static_cast<unsigned>(c.fallbackRows),
                        static_cast<unsigned>(c.fallbackReasons[0]),
                        static_cast<unsigned>(c.fallbackReasons[1]),
                        static_cast<unsigned>(c.fallbackReasons[2]),
                        static_cast<unsigned>(c.fallbackReasons[3]),
                        static_cast<unsigned>(c.fallbackReasons[4]),
                        static_cast<unsigned>(c.fallbackReasons[5]),
                        static_cast<unsigned>(c.fallbackReasons[6]),
                        static_cast<unsigned>(c.capturedRows),
                        static_cast<unsigned>(c.displayedRows),
                        static_cast<unsigned long long>(c.lineUs),
                        static_cast<unsigned long long>(c.registerUs),
                        static_cast<unsigned long long>(c.drawUs),
                        static_cast<unsigned long long>(c.busMirrorUs),
                        static_cast<unsigned long long>(c.conversionUs),
                        static_cast<unsigned long long>(c.prePvrUs),
                        static_cast<unsigned long long>(c.uploadUs),
                        static_cast<unsigned long long>(c.submitUs),
                        static_cast<unsigned long long>(c.syncBytes),
                        static_cast<unsigned long long>(c.syncWrites));
            }
            bridgeCostReported = true;
        }
#endif
        if (ppu_ && ppu_->presentation_observer() == this)
            ppu_->set_presentation_observer(previousPresentation_);
        if (bus_) bus_->exchange_write_observer(previousWrite_);
        // mGBADC owns process-lifetime PVR textures and callbacks. Keep the
        // shadow core alive until process exit so neither can dangle.
        ppu_ = nullptr;
        bus_ = nullptr;
    }

    bool owns_presentation() const { return pvrReady && !pvrFailed; }

    void scanline(const gba::GbaPpu& ppu, uint32_t y, uint16_t dispcnt,
                  const uint8_t* io, const uint8_t* vram,
                  const uint8_t* oam, const uint8_t* pal) override {
        if (previousPresentation_)
            previousPresentation_->scanline(ppu, y, dispcnt, io, vram, oam, pal);
    }

    bool native_scanline(const gba::GbaPpu& ppu, uint32_t y, uint16_t dispcnt,
                         const uint8_t* io, const uint8_t* vram,
                         const uint8_t* oam, const uint8_t* pal) override {
        if (!pvrReady || pvrFailed || !shadowGba || y >= kHeight) return false;
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        const uint64_t lineStart = timer_us_gettime64();
#endif
        seenRows[y] = true;
#if defined(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
        if (y == 0) {
            mixedBandValid_ = sma2_pvr_bridge::find_obj_free_row_band(
                oam, kMixedFallbackRows, &mixedBandStart_);
            std::printf("SMA2_NATIVE_PVR_MIXED_FIXTURE band_start=%u band_rows=%u obj_bounds_clear=%u\n",
                        mixedBandStart_, kMixedFallbackRows, mixedBandValid_ ? 1u : 0u);
        }
#endif
        if (y == 0) {
            lineStateHash = 1469598103934665603ull;
            writeHistoryHash = 1469598103934665603ull;
        }
        hash_bytes(lineStateHash, reinterpret_cast<const uint8_t*>(&y), sizeof(y));
        hash_bytes(lineStateHash, reinterpret_cast<const uint8_t*>(&dispcnt), sizeof(dispcnt));
        hash_bytes(lineStateHash, io, 0x60);
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        uint64_t phaseStart = timer_us_gettime64();
#endif
        synchronize_registers(dispcnt, io);
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.registerUs += timer_us_gettime64() - phaseStart;
#endif
        uint8_t reason = classify_fallback(dispcnt, io, oam);
#if defined(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
        if (mixedBandValid_ && y >= mixedBandStart_ &&
            y < mixedBandStart_ + kMixedFallbackRows)
            reason |= sma2_pvr_bridge::FallbackDiagnosticForce;
#endif
#if defined(SMA2_NATIVE_PVR_FORCE_FALLBACK)
        reason |= sma2_pvr_bridge::FallbackDiagnosticForce;
#endif
        fallbackReasons[y] = reason;
        PVRGBASMA2SetFallbackLine(y, reason != 0);
        activeRow_ = y;
        capturedNative_ = false;
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        phaseStart = timer_us_gettime64();
#endif
        sma2_native_pvr_draw_scanline(shadowGba, static_cast<int>(y));
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.drawUs += timer_us_gettime64() - phaseStart;
#endif
        nativeRows[y] = sma2_pvr_bridge::row_is_native_owned(reason, capturedNative_);
        if (previousPresentation_ && previousPresentation_->native_scanline(
                ppu, y, dispcnt, io, vram, oam, pal)) {
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
            activeBridgeCost.lineUs += timer_us_gettime64() - lineStart;
            ++activeBridgeCost.rows;
#endif
            return true;
        }
        if (!capturedNative_ && reason == 0)
            fallbackReasons[y] |= sma2_pvr_bridge::FallbackBackendDeclined;
        const bool skip = sma2_pvr_bridge::should_skip_software_composition(
            fallbackReasons[y], capturedNative_, skipSoftware_);
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.lineUs += timer_us_gettime64() - lineStart;
        ++activeBridgeCost.rows;
#endif
        return skip;
    }

    void frame_ready() override {
        if (previousPresentation_) previousPresentation_->frame_ready();
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        const uint64_t frameReadyStart = timer_us_gettime64();
#endif
#if defined(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG)
        print_resource_write_census();
        resourceWrites_.reset();
#endif
        if (!pvrReady || pvrFailed || !ppu_ || !ppu_->has_latched_framebuffer()) return;
        unsigned rows = 0, native = 0, forced = 0;
        uint32_t reasonCounts[7]{};
        for (unsigned y = 0; y < kHeight; ++y) {
            rows += seenRows[y];
            native += nativeRows[y];
            forced += fallbackReasons[y] != 0;
            for (unsigned bit = 0; bit < 7; ++bit)
                reasonCounts[bit] += (fallbackReasons[y] & (1u << bit)) != 0;
        }
        if (rows != kHeight) {
            std::printf("SMA2_NATIVE_PVR_FRAME_SKIP reason=incomplete_gba_frame rows=%u\n", rows);
            return;
        }
        const uint8_t* rgb = ppu_->latched_framebuffer();
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        uint64_t phaseStart = timer_us_gettime64();
#endif
        for (std::size_t i = 0; i < kPixels; ++i) {
            const uint8_t r = rgb[i * 3];
            const uint8_t g = rgb[i * 3 + 1];
            const uint8_t b = rgb[i * 3 + 2];
#ifdef COLOR_16_BIT
            fallbackBuffer[i] = static_cast<color_t>(((r >> 3) << 11) |
                                                      ((g >> 2) << 5) | (b >> 3));
#else
            fallbackBuffer[i] = static_cast<color_t>((b << 16) | (g << 8) | r);
#endif
        }
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.conversionUs += timer_us_gettime64() - phaseStart;
#endif
#if !defined(SMA2_NATIVE_PVR_COST_DIAG)
        uint64_t memoryHash = 1469598103934665603ull;
        hash_bytes(memoryHash, bus_->pal_ptr(), 1024);
        hash_bytes(memoryHash, bus_->vram_ptr(), 96 * 1024);
        hash_bytes(memoryHash, bus_->oam_ptr(), 1024);
        uint64_t ppuStateHash = lineStateHash;
        hash_bytes(ppuStateHash, reinterpret_cast<const uint8_t*>(&memoryHash), sizeof(memoryHash));
        hash_bytes(ppuStateHash, reinterpret_cast<const uint8_t*>(&writeHistoryHash), sizeof(writeHistoryHash));
        uint64_t rgb565Hash = 1469598103934665603ull;
        for (std::size_t i = 0; i < kPixels; ++i) {
            const uint8_t r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
            const uint16_t px = static_cast<uint16_t>(((r >> 3) << 11) |
                                                       ((g >> 2) << 5) | (b >> 3));
            rgb565Hash = (rgb565Hash ^ static_cast<uint8_t>(px)) * 1099511628211ull;
            rgb565Hash = (rgb565Hash ^ static_cast<uint8_t>(px >> 8)) * 1099511628211ull;
        }
#endif
        uint64_t upload = 0, submit = 0;
#if defined(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
        mixedBandValid_ = mixedBandValid_ &&
            sma2_pvr_bridge::obj_free_row_band(
                bus_->oam_ptr(), mixedBandStart_, kMixedFallbackRows);
        if (!mixedBandValid_)
            std::printf("SMA2_NATIVE_PVR_MIXED_FIXTURE_FAIL reason=obj_overlap_or_no_band\n");
#endif
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.prePvrUs = timer_us_gettime64() - frameReadyStart;
#endif
        // PVRGBAFrame publishes PVRGBALinesCaptured and display counters only
        // after it has consumed these 160 rows; callers validate afterward.
        PVRGBAFrame(shadowGba, &upload, &submit);
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.uploadUs = upload;
        activeBridgeCost.submitUs = submit;
        activeBridgeCost.guestFrame = ppu_->frame_count();
#endif
        ++framesSubmitted;
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
        activeBridgeCost.nativeRows = native;
        activeBridgeCost.fallbackRows = forced;
        activeBridgeCost.capturedRows = PVRGBALinesCaptured;
        activeBridgeCost.displayedRows = PVRGBADisplayLinesCaptured;
        activeBridgeCost.syncBytes = syncBytes;
        activeBridgeCost.syncWrites = syncWrites;
        for (unsigned bit = 0; bit < 7; ++bit)
            activeBridgeCost.fallbackReasons[bit] = reasonCounts[bit];
        activeBridgeCost.guestFrame = ppu_->frame_count();
        if (bridgeCostCount < bridgeCosts.size())
            bridgeCosts[bridgeCostCount++] = activeBridgeCost;
        activeBridgeCost = {};
#else
        std::printf("SMA2_NATIVE_PVR_FRAME frame=%llu guest_frame=%llu rows=%u native=%u fallback=%u forced_reasons=win:%u,bitmap:%u,blend:%u,bg_mosaic:%u,obj_mosaic:%u,backend:%u,diagnostic_force:%u captured=%u displayed=%u fallback_rows=%u software_oracle=%s ppu_state_fnv=%016llx rgb888_fnv=%016llx rgb565_fnv=%016llx sync_bytes=%llu sync_writes=%llu upload_us=%llu submit_us=%llu\n",
                    static_cast<unsigned long long>(framesSubmitted),
                    static_cast<unsigned long long>(ppu_->frame_count()), rows, native,
                    forced, static_cast<unsigned>(reasonCounts[0]),
                    static_cast<unsigned>(reasonCounts[1]),
                    static_cast<unsigned>(reasonCounts[2]),
                    static_cast<unsigned>(reasonCounts[3]),
                    static_cast<unsigned>(reasonCounts[4]),
                    static_cast<unsigned>(reasonCounts[5]),
                    static_cast<unsigned>(reasonCounts[6]),
                    PVRGBALinesCaptured, PVRGBADisplayLinesCaptured,
                    PVRGBAFallbackRows, skipSoftware_ ? "partial" : "complete",
                    static_cast<unsigned long long>(ppuStateHash),
                    static_cast<unsigned long long>(fnv1a(rgb, kPixels * 3)),
                    static_cast<unsigned long long>(rgb565Hash),
                    static_cast<unsigned long long>(syncBytes),
                    static_cast<unsigned long long>(syncWrites),
                    static_cast<unsigned long long>(upload),
                    static_cast<unsigned long long>(submit));
        if (forced) {
            std::printf("SMA2_NATIVE_PVR_FALLBACK_ROWS");
            for (unsigned y = 0; y < kHeight; ++y)
                if (fallbackReasons[y]) std::printf(" %u:%02x", y, fallbackReasons[y]);
            std::printf("\n");
        }
#endif
#if defined(SMA2_NATIVE_PVR_FRONT_CAPTURE)
        const uint64_t guestFrame = ppu_->frame_count();
        if (guestFrame >= kCaptureStartFrame && guestFrame <= kCaptureLastFrame) {
            bool countersValid = rows == kHeight &&
                PVRGBALinesCaptured == kHeight &&
                PVRGBADisplayLinesCaptured == kHeight;
#if defined(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
            const bool mixedCountsValid = mixedBandValid_ &&
                native == kHeight - kMixedFallbackRows &&
                forced == kMixedFallbackRows &&
                PVRGBAFallbackRows == kMixedFallbackRows;
            countersValid = countersValid && mixedCountsValid;
            std::printf("SMA2_NATIVE_PVR_MIXED_FIXTURE_CHECK frame=%llu native=%u fallback=%u backend_fallback=%u result=%s\n",
                        static_cast<unsigned long long>(guestFrame), native, forced,
                        PVRGBAFallbackRows, mixedCountsValid ? "PASS" : "FAIL");
#endif
            std::printf("SMA2_NATIVE_PVR_CAPTURE_COUNTERS frame=%llu rows=%u native=%u fallback=%u captured=%u displayed=%u fallback_rows=%u result=%s\n",
                        static_cast<unsigned long long>(guestFrame),
                        rows, native, forced, PVRGBALinesCaptured,
                        PVRGBADisplayLinesCaptured, PVRGBAFallbackRows,
                        countersValid ? "PASS" : "FAIL");
            std::printf("SMA2_NATIVE_PVR_LINE_CLASS frame=%llu",
                        static_cast<unsigned long long>(guestFrame));
            for (unsigned y = 0; y < kHeight; ++y) {
                if (!seenRows[y]) std::printf(" %u:missing", y);
                else if (nativeRows[y]) std::printf(" %u:native", y);
                else std::printf(" %u:fallback-%02x", y, fallbackReasons[y]);
            }
            std::printf("\n");
            std::fflush(stdout);
            const bool captureValid = countersValid && capture_gameplay_front(
                rgb, ppu_->frame_count(), ppuStateHash,
                fnv1a(rgb, kPixels * 3), rgb565Hash, rows, native, forced,
                upload, submit);
            std::printf("SMA2_NATIVE_PVR_ACCEPTANCE frame=%llu result=%s\n",
                        static_cast<unsigned long long>(guestFrame),
                        captureValid ? "PASS" : "FAIL");
            std::fflush(stdout);
            if (!captureValid) std::exit(1);
            if (guestFrame == kCaptureLastFrame) {
                std::printf("SMA2_NATIVE_PVR_SEQUENCE result=PASS first=%llu last=%llu frames=%llu\n",
                            static_cast<unsigned long long>(kCaptureStartFrame),
                            static_cast<unsigned long long>(kCaptureLastFrame),
                            static_cast<unsigned long long>(kCaptureFrameCount));
                std::fflush(stdout);
#if defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
                sma2_normal_bench_report_now();
#endif
                std::exit(0);
            }
        }
#endif
#if defined(SMA2_NATIVE_PVR_DIAG_STOP_FRAMES)
        if (framesSubmitted >= SMA2_NATIVE_PVR_DIAG_STOP_FRAMES) {
            const int ready = pvr_wait_ready();
            const int done = ready < 0 ? -1 : pvr_wait_render_done();
            std::printf("SMA2_NATIVE_PVR_DIAG_STOP frames=%llu ready=%d render_done=%d result=%s\n",
                        static_cast<unsigned long long>(framesSubmitted), ready, done,
                        ready >= 0 && done >= 0 ? "PASS" : "FAIL");
            std::fflush(stdout);
            std::exit(ready >= 0 && done >= 0 ? 0 : 1);
        }
#endif
        std::memset(nativeRows.data(), 0, nativeRows.size() * sizeof(nativeRows[0]));
        std::memset(seenRows.data(), 0, seenRows.size() * sizeof(seenRows[0]));
        std::memset(fallbackReasons.data(), 0, fallbackReasons.size());
    }

    void restored() noexcept override {
        if (previousPresentation_) previousPresentation_->restored();
        reseed();
    }

    bool on_bus_write(gba::BusWriteRegion region, uint32_t off, uint32_t addr,
                      uint8_t width, uint32_t oldValue,
                      uint32_t newValue) override {
        bool suppress = previousWrite_ && previousWrite_->on_bus_write(
            region, off, addr, width, oldValue, newValue);
        if (!suppress && shadowCore) {
            uint32_t mirrorAddress = 0;
            if (!sma2_pvr_bridge::mirror_address(region, off, width, &mirrorAddress))
                return suppress;
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
            const uint64_t mirrorStart = timer_us_gettime64();
#endif
#if defined(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG)
            resourceWrites_.record(region, ppu_ ? ppu_->vcount() : 160u,
                                   ppu_ && ppu_->in_hblank(),
                                   oldValue != newValue);
#endif
            if (width == 1 && shadowCore->busWrite8)
                shadowCore->busWrite8(shadowCore, mirrorAddress, static_cast<uint8_t>(newValue));
            else if (width == 2 && shadowCore->busWrite16)
                shadowCore->busWrite16(shadowCore, mirrorAddress, static_cast<uint16_t>(newValue));
            else if (width == 4 && shadowCore->busWrite32)
                shadowCore->busWrite32(shadowCore, mirrorAddress, newValue);
            hash_bytes(writeHistoryHash, reinterpret_cast<const uint8_t*>(&region), sizeof(region));
            hash_bytes(writeHistoryHash, reinterpret_cast<const uint8_t*>(&off), sizeof(off));
            hash_bytes(writeHistoryHash, reinterpret_cast<const uint8_t*>(&width), sizeof(width));
            hash_bytes(writeHistoryHash, reinterpret_cast<const uint8_t*>(&newValue), sizeof(newValue));
            syncBytes += width;
            ++syncWrites;
#if defined(SMA2_NATIVE_PVR_COST_DIAG)
            activeBridgeCost.busMirrorUs += timer_us_gettime64() - mirrorStart;
#endif
        }
        return suppress;
    }

private:
#if defined(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG)
    static void print_row_mask(const std::array<uint32_t, 160>& rowCounts) {
        for (int word = 4; word >= 0; --word) {
            uint32_t mask = 0;
            for (unsigned bit = 0; bit < 32; ++bit) {
                const unsigned row = static_cast<unsigned>(word) * 32 + bit;
                if (row < rowCounts.size() && rowCounts[row]) mask |= 1u << bit;
            }
            std::printf("%08x", mask);
        }
    }

    void print_resource_write_census() const {
        const auto& pal = resourceWrites_.kinds[0];
        const auto& vram = resourceWrites_.kinds[1];
        const auto& oam = resourceWrites_.kinds[2];
        const auto changed = [](const auto& counts) {
            uint32_t total = 0;
            for (const uint32_t count : counts) total += count;
            return total;
        };
        std::printf("SMA2_NATIVE_PVR_RESOURCE_WRITES frame=%llu"
                    " pal_writes=%u pal_changed=%u pal_vblank=%u pal_active=%u pal_hblank=%u pal_active_rows=",
                    static_cast<unsigned long long>(ppu_ ? ppu_->frame_count() : 0),
                    pal.writes, pal.changed, pal.vblankChanged,
                    changed(pal.activeChanged), changed(pal.hblankChanged));
        print_row_mask(pal.activeChanged);
        std::printf(" pal_hblank_rows="); print_row_mask(pal.hblankChanged);
        std::printf(" vram_writes=%u vram_changed=%u vram_vblank=%u vram_active=%u vram_hblank=%u vram_active_rows=",
                    vram.writes, vram.changed, vram.vblankChanged,
                    changed(vram.activeChanged), changed(vram.hblankChanged));
        print_row_mask(vram.activeChanged);
        std::printf(" vram_hblank_rows="); print_row_mask(vram.hblankChanged);
        std::printf(" oam_writes=%u oam_changed=%u oam_vblank=%u oam_active=%u oam_hblank=%u oam_active_rows=",
                    oam.writes, oam.changed, oam.vblankChanged,
                    changed(oam.activeChanged), changed(oam.hblankChanged));
        print_row_mask(oam.activeChanged);
        std::printf(" oam_hblank_rows="); print_row_mask(oam.hblankChanged);
        std::printf("\n");
    }
#endif

    static void hash_bytes(uint64_t& hash, const uint8_t* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 1099511628211ull;
    }

    static uint64_t fnv1a(const uint8_t* data, std::size_t size) {
        uint64_t hash = 1469598103934665603ull;
        hash_bytes(hash, data, size);
        return hash;
    }

    bool fail(const char* reason) {
        pvrFailed = true;
        std::printf("SMA2_NATIVE_PVR_INIT_FAIL reason=%s\n", reason);
        close();
        return false;
    }

    static bool line_hook(void*, int y, bool native) {
        if (bridge_ && y >= 0 && static_cast<unsigned>(y) == bridge_->activeRow_)
            bridge_->capturedNative_ = native;
        return native;
    }

    uint8_t classify_fallback(uint16_t dispcnt, const uint8_t* io,
                              const uint8_t* oam) const {
        return sma2_pvr_bridge::classify_fallback(dispcnt, io, oam);
    }

    static uint16_t read16(const uint8_t* p) {
        return sma2_pvr_bridge::read16(p);
    }

    void synchronize_registers(uint16_t dispcnt, const uint8_t* io) {
        sma2_pvr_bridge::synchronize_display_registers(
            dispcnt, io, [this](uint32_t address, uint16_t value) {
                shadowCore->busWrite16(shadowCore, address, value);
            });
        // The native backend does not apply GBA windows. For this exact
        // full-screen WIN0 state, every layer is visible and WININ masks all
        // color effects, so remove the otherwise-inactive effect mode from
        // the shadow renderer's register image.
        if (sma2_pvr_bridge::fullscreen_win0_layers_only(dispcnt, io))
            shadowCore->busWrite16(shadowCore, 0x04000050u,
                                   read16(io + 0x50) & ~0x00C0u);
    }

    void reseed() {
        if (!shadowCore || !bus_) return;
#if defined(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG)
        resourceWrites_.reset();
#endif
        if (shadowGba && shadowGba->video.renderer && shadowGba->video.renderer->reset)
            shadowGba->video.renderer->reset(shadowGba->video.renderer);
        shadowCore->setVideoBuffer(shadowCore, fallbackBuffer.data(), kWidth);
        PVRGBASMA2ResetFrameCapture();
        copy_words(0x05000000u, bus_->pal_ptr(), 1024);
        copy_words(0x06000000u, bus_->vram_ptr(), 96 * 1024);
        copy_words(0x07000000u, bus_->oam_ptr(), 1024);
        std::memset(nativeRows.data(), 0, nativeRows.size() * sizeof(nativeRows[0]));
        std::memset(seenRows.data(), 0, seenRows.size() * sizeof(seenRows[0]));
        std::memset(fallbackReasons.data(), 0, fallbackReasons.size());
        for (unsigned y = 0; y < kHeight; ++y) PVRGBASMA2SetFallbackLine(y, false);
        std::printf("SMA2_NATIVE_PVR_RESEED frame=%llu bytes=%llu\n",
                    static_cast<unsigned long long>(ppu_ ? ppu_->frame_count() : 0),
                    static_cast<unsigned long long>(1024 + 96 * 1024 + 1024));
    }

    void copy_words(uint32_t base, const uint8_t* bytes, std::size_t length) {
        for (std::size_t i = 0; i < length; i += 2) {
            shadowCore->busWrite16(shadowCore, base + static_cast<uint32_t>(i), read16(bytes + i));
        }
    }

    gba::GbaPpu* ppu_{};
    gba::GbaBus* bus_{};
    gba::GbaPpu::PresentationObserver* previousPresentation_{};
    gba::BusWriteObserver* previousWrite_{};
    unsigned activeRow_ = ~0u;
    bool capturedNative_{};
    bool skipSoftware_ = false;
#if defined(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG)
    sma2_pvr_bridge::ResourceWriteCensus resourceWrites_{};
#endif
#if defined(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
    unsigned mixedBandStart_{};
    bool mixedBandValid_{};
#endif
    inline static Bridge* bridge_{};
};
Bridge bridge;

} // namespace

bool native_pvr_bridge_open() {
    if (pvrReady) return true;
    Bridge::set_instance(&bridge);
    return bridge.open();
}
void native_pvr_bridge_close() { bridge.close(); }
bool native_pvr_bridge_owns_presentation() { return bridge.owns_presentation(); }
} // namespace gbarecomp
#endif
