#include "sma2_iwram_aot.h"
#include "iwram_code_guard.h"
#include <cinttypes>
#include <cstdio>

#include "runtime_arm.h"
#if defined(SMA2_IWRAM_AOT_ISLAND_SWITCH)
#include <fstream>
#ifndef SMA2_IWRAM_ISLAND_FLAG_PATH
#define SMA2_IWRAM_ISLAND_FLAG_PATH "/pc/sma2_iwram_island.txt"   // host tests override this
#endif
#endif

namespace sma2 {
namespace {
gba::GbaBus* g_bus = nullptr;
uint64_t g_native_entries = 0;
void count_native_entry(uint32_t pc) {
    if (pc - kIwramMixerVma < kIwramMixerLength) ++g_native_entries;
}
}
uint64_t iwram_aot_native_entries() { return g_native_entries; }

bool iwram_aot_install(gba::GbaBus& bus) {
    if (!bus.rom_ptr() || bus.rom_size() < kIwramMixerRomOffset + kIwramMixerLength) {
        std::fprintf(stderr, "SMA2_IWRAM_AOT_INSTALL_FAILED rom image too small (%zu)\n", bus.rom_size());
        return false;
    }
    gbarecomp::iwram_code_guard_install(bus, kIwramMixerVma, kIwramMixerLength,
                                        bus.rom_ptr() + kIwramMixerRomOffset);
    g_bus = &bus;
    g_native_entries = 0;
    g_runtime_fn_entry_hook = &count_native_entry;   // H5 diagnostic builds replace it with their own accounting
    const char* island_source = "compile-time";
#if defined(SMA2_IWRAM_AOT_ISLAND)
    g_runtime_native_island = 1;                     // force-interp runner + native dispatch only at the static IWRAM entries
#if defined(SMA2_IWRAM_AOT_ISLAND_SWITCH)
    // Layout-control switch (Arm C): the SAME ELF can run with native island execution disabled. The media file is
    // read once, here, before any checkpoint restore or timed window; its only effect is this one flag. Everything
    // else stays installed: generated code linked, guard, store barrier, eager validation, entry-count hook.
    {
        std::ifstream f(SMA2_IWRAM_ISLAND_FLAG_PATH, std::ios::binary);
        char c = 0;
        while (f && f.get(c)) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            if (c == '0') { g_runtime_native_island = 0; island_source = "media-file:0"; }
            else if (c == '1') { island_source = "media-file:1"; }
            else island_source = "media-file:ignored";
            break;
        }
        if (!f && island_source[0] == 'c') island_source = "default(no-file)";
    }
#endif
#endif
    std::fprintf(stderr, "SMA2_IWRAM_AOT_INSTALLED vma=0x%08x len=0x%x island=%d island_source=%s barrier=%s\n", kIwramMixerVma,
                 kIwramMixerLength, g_runtime_native_island, island_source,
#if defined(GBARECOMP_IWRAM_CODE_WATCH) && GBARECOMP_IWRAM_CODE_WATCH == 0
                 "OFF(diagnostic-ceiling-only)"
#else
                 "ON"
#endif
    );
    return true;
}

void iwram_aot_report(const char* tag) {
    if (!g_bus) { std::fprintf(stderr, "SMA2_IWRAM_AOT_STATS tag=%s installed=0\n", tag); return; }
    const auto& s = gbarecomp::iwram_code_guard_stats();
    std::fprintf(stderr,
        "SMA2_IWRAM_AOT_STATS tag=%s installed=1 valid=%d overlap_stores=%" PRIu64 " invalidations=%" PRIu64
        " hook_installs=%" PRIu64 " post_restore_reinstalls=%" PRIu64 " hook_calls=%" PRIu64
        " full_compares=%" PRIu64 " revalidations=%" PRIu64 " rejects=%" PRIu64 " cached_rejects=%" PRIu64
        " native_entries=%" PRIu64 "\n",
        tag, gbarecomp::iwram_code_guard_valid() ? 1 : 0, g_bus->iwram_code_watch_overlap_stores(),
        s.invalidations, s.hook_installs, s.post_restore_reinstalls, s.hook_calls, s.full_compares,
        s.revalidations, s.rejects, s.cached_rejects, g_native_entries);
}

}  // namespace sma2
