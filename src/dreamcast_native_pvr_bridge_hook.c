#include "mgba/internal/gba/gba.h"
#include "mgba/internal/gba/renderers/video-software.h"
#include "dreamcast_native_pvr_bridge_hook.h"

#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
static GBAVideoSoftwareLineHook previous_hook;
static sma2_native_pvr_line_result_fn result_hook;

static bool bridge_line_hook(struct GBAVideoSoftwareRenderer* renderer, int y) {
    bool native = previous_hook ? previous_hook(renderer, y) : false;
    return result_hook ? result_hook(renderer, y, native) : native;
}

void sma2_native_pvr_set_line_result_hook(sma2_native_pvr_line_result_fn hook) {
    previous_hook = GBAVideoSoftwareLineHookCallback;
    result_hook = hook;
    GBAVideoSoftwareLineHookCallback = bridge_line_hook;
}

void sma2_native_pvr_draw_scanline(void* gba_board, int y) {
    struct GBA* gba = (struct GBA*) gba_board;
    if (gba && gba->video.renderer && gba->video.renderer->drawScanline)
        gba->video.renderer->drawScanline(gba->video.renderer, y);
}
#endif
