#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace gbarecomp {

namespace pvr_present {
enum class Owner : uint8_t { Software, Pvr };
inline Owner select_owner(bool enabled, bool active) {
    return enabled && active ? Owner::Pvr : Owner::Software;
}

struct QuadPoint { float x, y, u, v; };
inline constexpr std::array<QuadPoint, 4> kScreenQuad{{
    {40.0f, 200.0f, 0.0f, 160.0f / 256.0f},
    {40.0f, 40.0f, 0.0f, 0.0f},
    {280.0f, 200.0f, 240.0f / 256.0f, 160.0f / 256.0f},
    {280.0f, 40.0f, 240.0f / 256.0f, 0.0f}
}};

constexpr unsigned kGameWidth = 240;
constexpr unsigned kGameHeight = 160;
constexpr unsigned kTextureWidth = 256;
constexpr unsigned kTextureHeight = 256;
constexpr std::size_t kTextureBytes = kTextureWidth * kTextureHeight * 2;

inline uint16_t rgb888_to_rgb565(const uint8_t* p) {
    return static_cast<uint16_t>(((p[0] >> 3) << 11) |
                                 ((p[1] >> 2) << 5) |
                                 (p[2] >> 3));
}

inline void convert(const uint8_t* src, uint16_t* dst) {
    for (unsigned y = 0; y < kTextureHeight; ++y) {
        uint16_t* row = dst + y * kTextureWidth;
        if (y >= kGameHeight) {
            for (unsigned x = 0; x < kTextureWidth; ++x) row[x] = 0;
            continue;
        }
        const uint8_t* in = src + y * kGameWidth * 3;
        for (unsigned x = 0; x < kGameWidth; ++x)
            row[x] = rgb888_to_rgb565(in + x * 3);
        for (unsigned x = kGameWidth; x < kTextureWidth; ++x) row[x] = 0;
    }
}

// Copy the completed GBA RGB565 image into the fixed PVR texture footprint.
// The PPU rows are tightly packed at 240 words; texture rows are 256 words.
inline void stage_rgb565(const uint16_t* src, uint16_t* dst) {
    for (unsigned y = 0; y < kTextureHeight; ++y) {
        uint16_t* row = dst + y * kTextureWidth;
        if (y < kGameHeight) {
            const uint16_t* in = src + y * kGameWidth;
            for (unsigned x = 0; x < kGameWidth; ++x) row[x] = in[x];
            for (unsigned x = kGameWidth; x < kTextureWidth; ++x) row[x] = 0;
        } else {
            for (unsigned x = 0; x < kTextureWidth; ++x) row[x] = 0;
        }
    }
}

inline void copy_rgb565_to_scanout(const uint16_t* src, uint16_t* dst,
                                   unsigned dst_width = 320,
                                   unsigned dst_height = 240) {
    if (!src || !dst || dst_width < kGameWidth || dst_height < kGameHeight)
        return;
    const unsigned x0 = (dst_width - kGameWidth) / 2;
    const unsigned y0 = (dst_height - kGameHeight) / 2;
    for (unsigned y = 0; y < kGameHeight; ++y) {
        uint16_t* out = dst + (y + y0) * dst_width + x0;
        const uint16_t* in = src + y * kGameWidth;
        for (unsigned x = 0; x < kGameWidth; ++x) out[x] = in[x];
    }
}
} // namespace pvr_present

class DreamcastPvrPresenter {
public:
    bool open();
    bool present(const uint8_t* rgb888);
    bool present(const uint16_t* rgb565);
    bool close();
    bool active() const { return active_; }
    bool cpu_fallback_safe() const { return cpu_fallback_safe_; }

private:
    bool present_staged_texture();
    bool active_ = false;
    bool cpu_fallback_safe_ = true;
    void* texture_ = nullptr;
    void* staging_ = nullptr;
    alignas(32) uint32_t header_[8]{};
};

} // namespace gbarecomp
