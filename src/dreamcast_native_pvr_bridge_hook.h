#pragma once

#include <stdbool.h>

typedef bool (*sma2_native_pvr_line_result_fn)(void*, int, bool);

#ifdef __cplusplus
extern "C" {
#endif
void sma2_native_pvr_set_line_result_hook(sma2_native_pvr_line_result_fn hook);
void sma2_native_pvr_draw_scanline(void* gba_board, int y);
#ifdef __cplusplus
}
#endif
