// SMA2 relocated-IWRAM AOT: installs GBARecomp's iwram_code_guard over the ROM-copied sound mixer.
#pragma once
#include "gba_bus.h"

namespace sma2 {

// ROM 0x0809DCDC..+0x360 is copied by the BIOS to IWRAM 0x03001518 at boot (see tools/sma2_iwram_mixer.toml).
constexpr uint32_t kIwramMixerVma = 0x03001518u;
constexpr uint32_t kIwramMixerRomOffset = 0x9DCDCu;
constexpr uint32_t kIwramMixerLength = 0x360u;

// Arms the guard (starts INVALID; the first execution inside the range does one full compare against the ROM
// image). Must run after the ROM is attached to the bus and BEFORE any checkpoint restore. Returns false and
// leaves the guard uninstalled if the ROM image is too small (then relocated IWRAM dispatch must not be used).
bool iwram_aot_install(gba::GbaBus& bus);
// One-line machine-readable summary (stderr) of the guard counters; safe to call when not installed.
void iwram_aot_report(const char* tag);
// Native generated IWRAM-function entries seen since install (cheap function-entry hook; no per-instruction work).
uint64_t iwram_aot_native_entries();

}  // namespace sma2
