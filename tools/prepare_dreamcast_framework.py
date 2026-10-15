#!/usr/bin/env python3
"""Stage the pinned local GBARecomp checkout with SMA2-only KOS adaptations."""
from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys


def replace_once(path: pathlib.Path, before: str, after: str) -> None:
    text = path.read_text()
    if text.count(before) != 1:
        raise SystemExit(f"expected one patch site in {path}: {before[:80]!r}")
    path.write_text(text.replace(before, after, 1))


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: prepare_dreamcast_framework.py GBARECOMP_ROOT DEST", file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1]).resolve()
    dest = pathlib.Path(sys.argv[2]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    print(f"GBARecomp source revision: {revision}")
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(source, dest, ignore=shutil.ignore_patterns(".git", "build", "build-*"))

    replace_once(
        dest / "src/gba/foreign_obj_focus.h",
        "    uint64_t hud_oam_mask_lo;\n    uint64_t hud_oam_mask_hi;",
        "    alignas(8) uint64_t hud_oam_mask_lo;\n    alignas(8) uint64_t hud_oam_mask_hi;",
    )
    replace_once(
        dest / "src/gba/gba_ppu.cpp",
        "std::min(31u, (tr * eva + br * evb + 8u) >> 4)",
        "std::min<uint32_t>(31u, (tr * eva + br * evb + 8u) >> 4)",
    )
    replace_once(
        dest / "src/gba/gba_ppu.cpp",
        "std::min(63u, (tg * eva + bg * evb + 8u) >> 4)",
        "std::min<uint32_t>(63u, (tg * eva + bg * evb + 8u) >> 4)",
    )
    replace_once(
        dest / "src/gba/gba_ppu.cpp",
        "std::min(31u, (tb * eva + bb * evb + 8u) >> 4)",
        "std::min<uint32_t>(31u, (tb * eva + bb * evb + 8u) >> 4)",
    )
    replace_once(
        dest / "src/debug/tcp_debug_server.cpp",
        "htonl(INADDR_LOOPBACK)",
        "htonl(0x7f000001u)",
    )
    view_config = dest / "src/runtime/view_config.h"
    for literal, variable in (("240u", "engine_max_width"),
                              ("160u", "engine_max_height")):
        old = f"std::max({literal}, {variable})"
        new = f"std::max<std::uint32_t>({literal}, {variable})"
        count = view_config.read_text().count(old)
        if count == 0:
            raise SystemExit(f"expected SH-4 max type site in {view_config}: {old}")
        view_config.write_text(view_config.read_text().replace(old, new))
    replace_once(
        view_config,
        "std::clamp(\n        game_max_width, 240u,",
        "std::clamp<std::uint32_t>(\n        game_max_width, 240u,",
    )
    replace_once(
        view_config,
        "std::clamp(\n        game_max_height, 160u,",
        "std::clamp<std::uint32_t>(\n        game_max_height, 160u,",
    )
    replace_once(
        view_config,
        "std::clamp(game_max_height, 160u,",
        "std::clamp<std::uint32_t>(game_max_height, 160u,",
    )
    replace_once(
        dest / "src/runtime/bios_hle.cpp",
        "div_t res = div(num, denom);",
        "ldiv_t res = ldiv(static_cast<long>(num), static_cast<long>(denom));",
    )
    ppu_header = dest / "src/gba/gba_ppu.h"
    replace_once(
        ppu_header,
        "    static constexpr uint32_t kMaxExtraX = 328;\n"
        "    static constexpr uint32_t kMaxExtraY = 352;",
        "    // SMA2's Dreamcast target is fixed to the native 240x160 view.\n"
        "    // Avoid reserving the desktop's 896x864 triple-byte surfaces.\n"
        "    static constexpr uint32_t kMaxExtraX = 0;\n"
        "    static constexpr uint32_t kMaxExtraY = 0;",
    )
    runtime_cpp = dest / "src/runtime/runtime.cpp"
    # Keep the interpreter as the only cart execution path and report enough
    # live state to distinguish a guest stall from slow emulation or missed input.
    replace_once(
        runtime_cpp,
        "                    ++frames_presented;\n"
        "                    if (args.frames >= 0 && frames_presented >= args.frames)\n",
        "                    ++frames_presented;\n"
        "#if defined(SMA2_DREAMCAST)\n"
        "                    if (frames_presented == 1 ||\n"
        "                        (frames_presented % 10) == 0) {\n"
        "                        std::fprintf(stderr,\n"
        "                            \"[dreamcast] frames=%d guest_frame=%llu pc=0x%08x state=%s cycles=%llu keyinput=0x%04x\\n\",\n"
        "                            frames_presented,\n"
        "                            static_cast<unsigned long long>(ppu.frame_count()),\n"
        "                            static_cast<unsigned>(g_cpu.R[15] & ~1u),\n"
        "                            (g_cpu.cpsr & CPSR_T_BIT) ? \"thumb\" : \"arm\",\n"
        "                            g_runtime_cycles,\n"
        "                            static_cast<unsigned>(bus.io().read16(0x130)));\n"
        "                        std::fflush(stderr);\n"
        "                    }\n"
        "#endif\n"
        "                    if (args.frames >= 0 && frames_presented >= args.frames)\n",
    )
    replace_once(
        runtime_cpp,
        "    gba::GbaBus bus;\n    gba::GbaPpu ppu;",
        "#if defined(SMA2_DREAMCAST)\n"
        "    static gba::GbaBus bus;\n"
        "    static gba::GbaPpu ppu;\n"
        "#else\n"
        "    gba::GbaBus bus;\n"
        "    gba::GbaPpu ppu;\n"
        "#endif",
    )
    for anchor, marker in [
        ("    Args args;", '    std::fprintf(stderr, "[sma2] runtime entered\\n");\n    Args args;'),
        ("    // Resolve BIOS via the picker chain", '    std::fprintf(stderr, "[sma2] config parsed\\n");\n    // Resolve BIOS via the picker chain'),
        ("    // Resolve ROM the same way.", '    std::fprintf(stderr, "[sma2] BIOS path resolved\\n");\n    // Resolve ROM the same way.'),
        ("    gba::GbaBios bios;", '    std::fprintf(stderr, "[sma2] ROM path resolved\\n");\n    gba::GbaBios bios;'),
        ("    std::vector<uint8_t> rom;", '    std::fprintf(stderr, "[sma2] BIOS loaded\\n");\n    std::vector<uint8_t> rom;'),
        ("    if (rom.size() < 0xC0", '    std::fprintf(stderr, "[sma2] ROM loaded: %zu bytes\\n", rom.size());\n    if (rom.size() < 0xC0'),
        ("    if (!netplay) {", '    if (!netplay) {'),
        ("#if defined(SMA2_DREAMCAST)\n    static gba::GbaBus bus;", '    std::fprintf(stderr, "[sma2] constructing GBA bus and PPU\\n");\n#if defined(SMA2_DREAMCAST)\n    static gba::GbaBus bus;'),
    ]:
        if anchor == "    if (!netplay) {":
            continue
        replace_once(runtime_cpp, anchor, marker)
    replace_once(
        runtime_cpp,
        "        auto r = resolve_asset(args.bios, spec, argv[0]);",
        '        std::fprintf(stderr, "[sma2] resolving BIOS path: %s\\n", args.bios.c_str());\n'
        "        auto r = resolve_asset(args.bios, spec, argv[0]);",
    )
    host_platform = dest / "src/runtime/host_platform.cpp"
    replace_once(
        host_platform,
        "    std::FILE* f = std::fopen(path.c_str(), \"rb\");",
        '    std::fprintf(stderr, "[asset] opening %s\\n", path.c_str());\n'
        "    std::FILE* f = std::fopen(path.c_str(), \"rb\");",
    )
    replace_once(
        host_platform,
        "    long sz = std::ftell(f);",
        '    long sz = std::ftell(f);\n'
        '    std::fprintf(stderr, "[asset] ftell %ld\\n", sz);',
    )
    host_window = dest / "src/runtime/host_window.cpp"
    replace_once(
        host_window,
        "#else  // !GBARECOMP_HAVE_SDL2 — stub backend\n\nnamespace gbarecomp {\n",
        "#else  // !GBARECOMP_HAVE_SDL2 — stub backend\n\n"
        "#if defined(SMA2_DREAMCAST)\n"
        '#include "dreamcast_host_window.inc"\n'
        "#else\n\nnamespace gbarecomp {\n",
    )
    replace_once(
        host_window,
        "}  // namespace gbarecomp\n\n#endif  // GBARECOMP_HAVE_SDL2",
        "}  // namespace gbarecomp\n\n#endif // SMA2_DREAMCAST\n\n"
        "#endif  // GBARECOMP_HAVE_SDL2",
    )
    overlay_compile = dest / "src/runtime/overlay_compile.cpp"
    original_compile = overlay_compile.read_text()
    stub = '''#include "overlay_compile.h"
#if defined(SMA2_DREAMCAST)
namespace gbarecomp {
const char* heal_backend_name(HealBackend b) {
    return b == HealBackend::Tcc ? "tcc" : "gcc";
}
bool overlay_bundled_tcc_available() { return false; }
bool overlay_compile_one(const OverlayWorkItem&, const std::string&,
                         const GbaOverlayCallbacks*, bool, HealBackend,
                         OverlayCompiled*, std::string* err) {
    if (err) *err = "runtime code generation is disabled on Dreamcast";
    return false;
}
}
#else
'''
    overlay_compile.write_text(
        stub + original_compile + "\n#endif // SMA2_DREAMCAST\n"
    )
    print(f"Staged SMA2 compatibility overlay: {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
