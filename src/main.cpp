#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "runtime.h"
#include "smw_extended_view.h"

#if defined(SMA2_DREAMCAST)
#include <arch/arch.h>
#include <dc/maple/controller.h>
#include <kos/thread.h>
#endif

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

static int run_sma2(int argc, char** argv) {
#if defined(SMA2_DREAMCAST)
    // Native cart dispatch is intentionally disabled until the interpreter
    // port has established correct startup, video, and controller behavior.
    setenv("GBARECOMP_FORCE_INTERP", "1", 1);
    // dcload's current working directory is not the mapped /pc root, and some
    // KOS launch paths do not propagate the optional argv tail. Make the
    // mounted project assets explicit so startup is deterministic.
    std::vector<std::string> dc_args;
    dc_args.emplace_back(argc > 0 && argv && argv[0]
                             ? argv[0] : "SuperMarioAdvance2Recomp.elf");
    dc_args.emplace_back("--config");
    dc_args.emplace_back("/pc/game.toml");
    dc_args.emplace_back("--bios");
    dc_args.emplace_back("/pc/gbarecomp/bios/gba_bios.bin");
    dc_args.emplace_back("--rom");
    dc_args.emplace_back("/pc/roms/super_mario_world_usa.gba");
    std::vector<char*> dc_argv;
    dc_argv.reserve(dc_args.size());
    for (auto& arg : dc_args) dc_argv.push_back(arg.data());
    argc = static_cast<int>(dc_argv.size());
    argv = dc_argv.data();
#endif
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "SuperMarioWorldRecomp [--bios <path>] [--rom <path>] [game.toml]\n");
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
#if defined(SMA2_CART_AOT_DIAG)
    // Upstream interactive replay capture allocates its 32 MiB writer ring
    // before the first guest step. Keep the pre-replay startup behavior for
    // this bounded hardware diagnostic; normal builds retain the default.
    opts.diagnostic_replay_default = false;
#endif
#if defined(SMA2_SH4_DYNAREC)
    // The bounded SH-4 dynarec differential is diagnostic-only as well.
    opts.diagnostic_replay_default = false;
#endif
    opts.builtin_game_name = "Super Mario Advance 2: Super Mario World";
    opts.builtin_rom_sha1 = "5101ddf223d1d918928fe1f306b63a42ada14a5e";
    opts.builtin_rom_crc32 = 0x5206880Au;
    opts.mod_game_id = "super-mario-world-us";
    opts.mod_owns_adaptive_view = true;

    // Super Mario World's renderer streams scrolling tile maps around the
    // native viewport. Adaptive view exposes the useful surrounding columns
    // while keeping the original 240x160 mode as the faithful default.
    opts.max_resize_view_width = 288;
    opts.resize_driven_view = true;
    opts.launcher_expose_widescreen = false;
    opts.launcher_expose_adaptive_view = false;
    opts.extended_view_init = smw::install_extended_view;

    opts.launcher_region = "USA";
    opts.launcher_game_config = "game.toml";
    opts.launcher_save_path = "saves/super_mario_world_usa.sav";

#if defined(GBAGAME_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    if (game_launcher_preboot(args, opts)) return 0;
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    return gbarecomp::run_game(argc, argv, opts);
#endif
}

#if defined(SMA2_DREAMCAST)
namespace {
struct LaunchArgs { int argc; char** argv; };

void* run_with_port_stack(void* opaque) {
    const auto* args = static_cast<const LaunchArgs*>(opaque);
    const int result = run_sma2(args->argc, args->argv);
    return reinterpret_cast<void*>(static_cast<intptr_t>(result));
}
} // namespace

int main(int argc, char** argv) {
    cont_btn_callback(0,
        CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
        (cont_btn_callback_t)arch_exit);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "[sma2] main entered\n");
    LaunchArgs args{argc, argv};
    kthread_attr_t attr{};
    attr.stack_size = 512 * 1024;
    attr.label = "SMA2 interpreter";
    kthread_t* thread = thd_create_ex(&attr, run_with_port_stack, &args);
    if (!thread) {
        std::fprintf(stderr, "fatal: could not allocate 512 KiB SMA2 stack\n");
        return 1;
    }
    void* result = nullptr;
    const int join_result = thd_join(thread, &result);
    if (join_result != 0) {
        std::fprintf(stderr, "fatal: could not join SMA2 runtime thread (%d)\n",
                     join_result);
        return 1;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(result));
}
#else
int main(int argc, char** argv) { return run_sma2(argc, argv); }
#endif
