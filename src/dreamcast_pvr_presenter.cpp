#include "dreamcast_pvr_presenter.h"

#include <dc/pvr.h>
#include <malloc.h>
#include <cstring>

namespace gbarecomp {
namespace {
} // namespace

bool DreamcastPvrPresenter::open() {
    if (active_) return true;
    staging_ = memalign(32, pvr_present::kTextureBytes);
    if (!staging_) return false;
    std::memset(staging_, 0, pvr_present::kTextureBytes);
    // KOS has no public initialized-state query. A failed init may mean a
    // different PVR owner already exists, so CPU fallback is not yet proven.
    cpu_fallback_safe_ = false;
    if (pvr_init_defaults() < 0) {
        std::free(staging_);
        staging_ = nullptr;
        return false;
    }
    active_ = true;
    cpu_fallback_safe_ = false;
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);
    texture_ = pvr_mem_malloc(pvr_present::kTextureBytes);
    if (!texture_) {
        close();
        return false;
    }

    pvr_poly_cxt_t context;
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                     PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                     pvr_present::kTextureWidth, pvr_present::kTextureHeight,
                     static_cast<pvr_ptr_t>(texture_), PVR_FILTER_NONE);
    context.txr.env = PVR_TXRENV_REPLACE;
    pvr_poly_hdr_t header;
    pvr_poly_compile(&header, &context);
    std::memcpy(header_, &header, sizeof(header));
    return true;
}

bool DreamcastPvrPresenter::present(const uint8_t* rgb888) {
    if (!active_ || !rgb888) return false;
    pvr_present::convert(rgb888, static_cast<uint16_t*>(staging_));
    return present_staged_texture();
}

bool DreamcastPvrPresenter::present(const uint16_t* rgb565) {
    if (!active_ || !rgb565) return false;
    pvr_present::stage_rgb565(rgb565, static_cast<uint16_t*>(staging_));
    return present_staged_texture();
}

bool DreamcastPvrPresenter::present_staged_texture() {
    if (!active_ || !staging_ || !texture_) return false;
    if (pvr_wait_render_done() < 0 || pvr_wait_ready() < 0) {
        close();
        return false;
    }

    pvr_txr_load(staging_, static_cast<pvr_ptr_t>(texture_), pvr_present::kTextureBytes);

    pvr_poly_hdr_t* header = reinterpret_cast<pvr_poly_hdr_t*>(header_);
    pvr_vertex_t vertices[4]{};
    for (unsigned i = 0; i < pvr_present::kScreenQuad.size(); ++i) {
        const auto& point = pvr_present::kScreenQuad[i];
        vertices[i] = { .flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                        .x = point.x, .y = point.y, .z = 1.0f,
                        .u = point.u, .v = point.v, .argb = 0xFFFFFFFF };
    }

    pvr_scene_begin();
    if (pvr_list_begin(PVR_LIST_OP_POLY) < 0) {
        close();
        return false;
    }
    pvr_prim(header, sizeof(*header));
    for (const auto& vertex : vertices) pvr_prim(&vertex, sizeof(vertex));
    if (pvr_list_finish() < 0) {
        close();
        return false;
    }
    if (pvr_scene_finish() < 0) {
        close();
        return false;
    }
    return true;
}

bool DreamcastPvrPresenter::close() {
    if (active_) {
        // KOS shutdown resets the PVR before invalidating its texture pool.
        // The same vid_mode remains installed and the scanout base returns to 0.
        if (pvr_shutdown() < 0) {
            cpu_fallback_safe_ = false;
            return false;
        }
        active_ = false;
        texture_ = nullptr;
        cpu_fallback_safe_ = true;
    }
    std::free(staging_);
    staging_ = nullptr;
    return cpu_fallback_safe_;
}

} // namespace gbarecomp
