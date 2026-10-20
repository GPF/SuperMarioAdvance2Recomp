#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "runtime.h"
#include "smw_extended_view.h"

#if defined(SMA2_DREAMCAST)
#include <arch/arch.h>
#include <dc/maple/controller.h>
#include <kos/thread.h>
#endif
#if defined(SMA2_DREAMCAST) && defined(SMA2_SH4_DYNAREC)
// Dreamcast CPU backend selection. kos-tool cannot pass an argv tail to main(),
// so the choice is read from a small file on the DC filesystem, with the
// environment as a fallback for host runs. Interpreter stays the default:
// no file, empty file, or anything that is not "jit" means interpreter.
//   /pc/sma2_cpu_backend.txt  -> first non-space char 'j' = SH-4 dynarec
//                                first non-space char 'i' = interpreter
static const char* sma2_backend_file = "/pc/sma2_cpu_backend.txt";

static bool sma2_backend_file_jit() {
    std::ifstream in(sma2_backend_file, std::ios::binary);
    if (!in) return false;
    char c = 0;
    while (in.get(c)) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return (c == 'j' || c == 'J');
    }
    return false;
}
#endif


#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

#if defined(SMA2_DREAMCAST)
static std::string sma2_asset_path(const char* relative_path) {
    const char* roots[] = {"/pc/", "/cd/"};
    for (const char* root : roots) {
        const std::string path = std::string(root) + relative_path;
        std::ifstream asset(path, std::ios::binary);
        if (asset) {
            std::fprintf(stderr, "[sma2] asset found: %s\n", path.c_str());
            return path;
        }
    }

    const std::string fallback = std::string("/cd/") + relative_path;
    std::fprintf(stderr, "[sma2] asset missing under /pc and /cd: %s\n",
                 relative_path);
    return fallback;
}
#endif

static int run_sma2(int argc, char** argv) {
#if defined(SMA2_DREAMCAST)
    // The explicit parity diagnostic exercises static dispatch misses while
    // requiring every guest instruction to be interpreted. Normal builds keep
    // the production interpreter-forced policy below.
#if defined(SMA2_AOT_MODE_PARITY_DIAG)
    setenv("GBARECOMP_FORCE_INTERP", "0", 1);
#else
    setenv("GBARECOMP_FORCE_INTERP", "1", 1);
#endif
#if defined(SMA2_GATE9D_VERBOSE)
    setenv("GBARECOMP_SELFHEAL_VERBOSE",
           SMA2_GATE9D_VERBOSE ? "1" : "0", 1);
#endif
#if defined(SMA2_DEV_CHECKPOINT)
    std::fprintf(stderr,
                 "[dreamcast-checkpoint] Y-button save enabled: "
                 "/pc/sma2_checkpoint.bin\n");
#endif
    // Prefer dcload's /pc host filesystem, then use KOS's /cd disc mount.
    // Resolve each input independently so mixed launch setups still work.
    std::vector<std::string> dc_args;
    dc_args.emplace_back(argc > 0 && argv && argv[0]
                             ? argv[0] : "SuperMarioAdvance2Recomp.elf");
    dc_args.emplace_back("--config");
    dc_args.emplace_back(sma2_asset_path("game.toml"));
    dc_args.emplace_back("--bios");
    dc_args.emplace_back(sma2_asset_path("gbarecomp/bios/gba_bios.bin"));
    dc_args.emplace_back("--rom");
    dc_args.emplace_back(sma2_asset_path("roms/super_mario_world_usa.gba"));
#if defined(SMA2_SNAPSHOT_H5_DIAG)
    // The exact-instruction diagnostic terminates via its runtime hook; this
    // independent frame cap is a fail-safe bound.
    dc_args.emplace_back("--window");
    dc_args.emplace_back("--frames");
#if defined(SMA2_SNAPSHOT_H5_MIXER_EPISODES) && SMA2_SNAPSHOT_H5_MIXER_EPISODES > 0
    // Event-bounded window: N mixer episodes span about N+1 frames; keep the fail-safe bound above that.
    dc_args.emplace_back(std::to_string(SMA2_SNAPSHOT_H5_MIXER_EPISODES + 4));
#else
    dc_args.emplace_back("2");
#endif
#elif defined(SMA2_SNAPSHOT_INPUT_DIAG)
    // Synthetic input fixture: sample one release, a three-pump Y hold, a
    // release, and a second Y edge. Five guest frames bound the run.
    dc_args.emplace_back("--window");
    dc_args.emplace_back("--frames");
    dc_args.emplace_back("5");
#elif defined(SMA2_NORMAL_BENCH_FRAMES) && SMA2_NORMAL_BENCH_FRAMES > 0
    // Normal-gameplay benchmark: the production interpreter-forced runner for a bounded number of frames from the
    // autoloaded checkpoint. No H5/parity hooks; the window must be requested explicitly when --frames is set.
    dc_args.emplace_back("--window");
    dc_args.emplace_back("--frames");
    dc_args.emplace_back(std::to_string(SMA2_NORMAL_BENCH_FRAMES));
#elif defined(SMA2_MEMORY_ALLOC_DIAG)
    // Keep the allocation probe finite and interpreter-only on hardware.
    // The explicit window request is necessary when --frames is set: the
    // runtime otherwise skips its automatic window creation and its startup
    // checkpoint hook.
    dc_args.emplace_back("--window");
    dc_args.emplace_back("--frames");
    dc_args.emplace_back("1");
    dc_args.emplace_back("--quiet");
#endif
#if defined(SMA2_SH4_DYNAREC)
    // kos-tool cannot hand main() an argv tail, so the backend choice comes from
    // the file above (or the environment, for host runs). Default: interpreter.
    {
        bool jit = sma2_backend_file_jit();
        if (const char* be = std::getenv("SMA2_CPU_BACKEND"))
            jit = jit || (be[0] == 'j' || be[0] == 'J');
        setenv("SMA2_CPU_BACKEND", jit ? "jit" : "interp", 1);
    }
#endif
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
                "SuperMarioWorldRecomp [--bios <path>] [--rom <path>] "
                "[--cpu interp|jit] [game.toml]\n");
            return 0;
        }
#if defined(SMA2_SH4_DYNAREC)
        // Runtime CPU backend select. The interpreter is the default; --cpu jit
        // asks for the vendored SH-4 dynarec (see src/sh4jit/). Nothing enables
        // it implicitly for a normal build.
        if (std::strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            const char* v = argv[i + 1];
            if (std::strcmp(v, "jit") == 0)
                setenv("SMA2_CPU_BACKEND", "jit", 1);
            else
                setenv("SMA2_CPU_BACKEND", "interp", 1);
            ++i;
            continue;
        }
#endif
    }

    gbarecomp::RunOptions opts;
#if defined(SMA2_CART_AOT_DIAG)
    // Upstream interactive replay capture allocates its 32 MiB writer ring
    // before the first guest step. The bounded AOT diagnostic exits at that
    // first step, so keep the pre-replay diagnostic startup behavior here.
    // Normal SMA2 builds retain GBARecomp's default replay setting.
    opts.diagnostic_replay_default = false;
#endif
#if defined(SMA2_SH4_DYNAREC)
    // The bounded SH-4 dynarec differential is also a diagnostic build. Its
    // staged comparisons must not allocate the interactive replay writer.
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
#if defined(SMA2_MEMORY_ALLOC_DIAG)
extern "C" void sma2_memory_alloc_diag_dump();
#endif
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
#if defined(SMA2_MEMORY_ALLOC_DIAG)
        sma2_memory_alloc_diag_dump();
#endif
        return 1;
    }
    void* result = nullptr;
    const int join_result = thd_join(thread, &result);
    if (join_result != 0) {
        std::fprintf(stderr, "fatal: could not join SMA2 runtime thread (%d)\n",
                     join_result);
#if defined(SMA2_MEMORY_ALLOC_DIAG)
        sma2_memory_alloc_diag_dump();
#endif
        return 1;
    }
#if defined(SMA2_MEMORY_ALLOC_DIAG)
    sma2_memory_alloc_diag_dump();
#endif
    return static_cast<int>(reinterpret_cast<intptr_t>(result));
}
#else
int main(int argc, char** argv) { return run_sma2(argc, argv); }
#endif
