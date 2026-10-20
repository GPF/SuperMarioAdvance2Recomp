#pragma once

#if defined(SMA2_NATIVE_PVR_GBA_RENDER)
#include "dreamcast_native_pvr_bridge_hook.h"

namespace gbarecomp {
bool native_pvr_bridge_open();
void native_pvr_bridge_close();
bool native_pvr_bridge_owns_presentation();
}
#endif
