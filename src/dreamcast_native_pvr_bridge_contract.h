#pragma once

#include "gba_bus.h"

#include <array>
#include <cstdint>

namespace gbarecomp::sma2_pvr_bridge {

enum FallbackReason : uint8_t {
    FallbackNone = 0,
    FallbackWindow = 1u << 0,
    FallbackBitmap = 1u << 1,
    FallbackBlend = 1u << 2,
    FallbackBgMosaic = 1u << 3,
    FallbackObjMosaic = 1u << 4,
    FallbackBackendDeclined = 1u << 5,
    FallbackDiagnosticForce = 1u << 6,
};

inline uint16_t read16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

struct ResourceWriteCensus {
    static constexpr unsigned kRows = 160;
    static constexpr unsigned kKinds = 3;
    struct Counts {
        uint32_t writes{};
        uint32_t changed{};
        uint32_t vblankChanged{};
        std::array<uint32_t, kRows> activeChanged{};
        std::array<uint32_t, kRows> hblankChanged{};
    };
    std::array<Counts, kKinds> kinds{};

    void reset() { kinds = {}; }

    void record(gba::BusWriteRegion region, uint32_t y, bool hblank,
                bool changed) {
        unsigned kind = 0;
        switch (region) {
            case gba::BusWriteRegion::Pal: kind = 0; break;
            case gba::BusWriteRegion::Vram: kind = 1; break;
            case gba::BusWriteRegion::Oam: kind = 2; break;
            default: return;
        }
        Counts& c = kinds[kind];
        ++c.writes;
        if (!changed) return;
        ++c.changed;
        if (y >= kRows) {
            ++c.vblankChanged;
        } else if (hblank) {
            ++c.hblankChanged[y];
        } else {
            ++c.activeChanged[y];
        }
    }
};

inline constexpr std::array<uint8_t, 38> kDisplayRegisterOffsets = {
    0x08,0x0A,0x0C,0x0E, 0x10,0x12,0x14,0x16,0x18,0x1A,0x1C,0x1E,
    0x20,0x22,0x24,0x26,0x28,0x2A,0x2C,0x2E,0x30,0x32,0x34,0x36,
    0x38,0x3A,0x3C,0x3E, 0x40,0x42,0x44,0x46,0x48,0x4A,0x4C,
    0x50,0x52,0x54
};

template <typename Write16>
inline void synchronize_display_registers(uint16_t dispcnt, const uint8_t* io,
                                          Write16 write16) {
    for (const uint8_t offset : kDisplayRegisterOffsets)
        write16(0x04000000u + offset, read16(io + offset));
    write16(0x04000000u, dispcnt);
}

// Return the mGBA bus address for a palette/VRAM/OAM write. Other regions are
// intentionally not mirrored by this rendering-only shadow core.
inline bool mirror_address(gba::BusWriteRegion region, uint32_t offset,
                           uint8_t width, uint32_t* address) {
    if (!address || (width != 1 && width != 2 && width != 4)) return false;
    uint32_t base = 0;
    uint32_t size = 0;
    switch (region) {
        case gba::BusWriteRegion::Pal:  base = 0x05000000u; size = 0x400u; break;
        case gba::BusWriteRegion::Vram: base = 0x06000000u; size = 0x18000u; break;
        case gba::BusWriteRegion::Oam:  base = 0x07000000u; size = 0x400u; break;
        default: return false;
    }
    if (offset > size || width > size - offset) return false;
    *address = base + offset;
    return true;
}

// The GBA software PPU clamps an inverted or over-range end coordinate to
// the visible edge. Recognize only the one window configuration whose layer
// mask can be represented by the native backend: WIN0 covers the full screen,
// allows every BG/OBJ layer, and masks color effects.
inline bool fullscreen_win0_layers_only(uint16_t dispcnt, const uint8_t* io) {
    if ((dispcnt & 0xE000u) != 0x2000u) return false;
    const unsigned x1 = read16(io + 0x40) >> 8;
    unsigned x2 = read16(io + 0x40) & 0xFFu;
    const unsigned y1 = read16(io + 0x44) >> 8;
    unsigned y2 = read16(io + 0x44) & 0xFFu;
    if (x2 > 240 || x1 > x2) x2 = 240;
    if (y2 > 160 || y1 > y2) y2 = 160;
    return x1 == 0 && x2 == 240 && y1 == 0 && y2 == 160 &&
           (read16(io + 0x48) & 0x3Fu) == 0x1Fu;
}

inline uint8_t classify_fallback(uint16_t dispcnt, const uint8_t* io,
                                 const uint8_t* oam) {
    uint8_t reasons = FallbackNone;
    const bool representableWindow = fullscreen_win0_layers_only(dispcnt, io);
    if ((dispcnt & 0xE000u) && !representableWindow) reasons |= FallbackWindow;
    if ((dispcnt & 7) > 2) reasons |= FallbackBitmap;
    if ((read16(io + 0x50) & 0x00C0) &&
        !(representableWindow && !(read16(io + 0x48) & 0x20u)))
        reasons |= FallbackBlend;
    const bool mosaicActive = read16(io + 0x4C) != 0;
    for (unsigned bg = 0; bg < 4; ++bg)
        if (mosaicActive && (read16(io + 0x08 + bg * 2) & 0x0040))
            reasons |= FallbackBgMosaic;
    if (mosaicActive && (dispcnt & 0x1000)) {
        for (unsigned obj = 0; obj < 128; ++obj) {
            if (read16(oam + obj * 8) & 0x1000) {
                reasons |= FallbackObjMosaic;
                break;
            }
        }
    }
    return reasons;
}

// A row is PVR-owned only when the bridge's effect classifier accepts it and
// mGBA's native line hook actually captured it. In native-skip mode this is
// also the sole condition that permits GBARecomp to omit software pixels.
inline bool row_is_native_owned(uint8_t fallbackReasons, bool capturedNative) {
    return fallbackReasons == FallbackNone && capturedNative;
}

inline bool should_skip_software_composition(uint8_t fallbackReasons,
                                             bool capturedNative,
                                             bool nativeSkipEnabled) {
    return nativeSkipEnabled && row_is_native_owned(fallbackReasons, capturedNative);
}

inline bool obj_free_row_band(const uint8_t* oam, unsigned start,
                              unsigned bandHeight) {
    if (!oam || !bandHeight || bandHeight > 160 || start + bandHeight > 160)
        return false;
    bool occupied[160]{};
    constexpr unsigned dimensions[3][4][2] = {
        {{8,8}, {16,16}, {32,32}, {64,64}},
        {{16,8}, {32,8}, {32,16}, {64,32}},
        {{8,16}, {8,32}, {16,32}, {32,64}},
    };
    for (unsigned obj = 0; obj < 128; ++obj) {
        const uint16_t attr0 = read16(oam + obj * 8);
        const uint16_t attr1 = read16(oam + obj * 8 + 2);
        const bool affine = (attr0 & 0x0100u) != 0;
        if (!affine && (attr0 & 0x0200u)) continue;
        const unsigned shape = attr0 >> 14;
        if (shape >= 3) continue;
        const unsigned size = attr1 >> 14;
        unsigned height = dimensions[shape][size][1];
        if (affine && (attr0 & 0x0200u)) height *= 2;
        int top = attr0 & 0x00FFu;
        if (top >= 160) top -= 256;
        for (int y = top; y < top + static_cast<int>(height); ++y)
            if (y >= 0 && y < 160) occupied[y] = true;
    }
    for (unsigned y = start; y < start + bandHeight; ++y)
        if (occupied[y]) return false;
    return true;
}

// Select the first visible band of `bandHeight` rows that cannot intersect
// any vertically bounded OBJ. The native renderer suppresses whole OBJ
// quads that touch fallback rows, so this keeps the fixture focused on
// fallback-buffer ownership rather than that known limitation.
inline bool find_obj_free_row_band(const uint8_t* oam, unsigned bandHeight,
                                   unsigned* bandStart) {
    if (!oam || !bandStart || !bandHeight || bandHeight > 160) return false;
    for (unsigned start = 0; start + bandHeight <= 160; ++start) {
        if (obj_free_row_band(oam, start, bandHeight)) {
            *bandStart = start;
            return true;
        }
    }
    return false;
}

} // namespace gbarecomp::sma2_pvr_bridge
