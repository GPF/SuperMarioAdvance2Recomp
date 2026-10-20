cmake_minimum_required(VERSION 3.20)
project(SuperMarioWorldRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to this game's gbarecomp worktree")
set(SMA2_RGB565_SOURCE_ROOT "${GBARECOMP_ROOT}"
    CACHE PATH "Unstaged GBARecomp source used to prepare the Dreamcast overlay")
set(SMA2_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    CACHE PATH "Directory containing GBARecomp-generated cart sources")
option(SMA2_REQUIRE_GENERATED_DISPATCH
       "Fail configuration unless a non-empty generated cart dispatch table is linked" OFF)
option(SMA2_DREAMCAST "Build the interpreter-first KallistiOS port" OFF)
option(SMA2_PVR_PRESENT "Use the opt-in KOS PVR presenter for completed software frames" OFF)
option(SMA2_NATIVE_PVR_GBA_RENDER
       "Use the opt-in mGBADC native GBA BG/OBJ Dreamcast PVR renderer" OFF)
option(SMA2_NATIVE_PVR_SKIP_SOFTWARE
       "Skip GBARecomp software composition on native-owned PVR scanlines" OFF)
option(SMA2_NATIVE_PVR_FRONT_CAPTURE
       "Capture and compare the completed native PVR frame on Dreamcast" OFF)
set(SMA2_NATIVE_PVR_CAPTURE_FRAMES "1" CACHE STRING
    "Number of consecutive completed gameplay frames to capture (front capture only)")
set(SMA2_NATIVE_PVR_CAPTURE_START_FRAME "201" CACHE STRING
    "First guest frame captured by the native-PVR front capture")
option(SMA2_NATIVE_PVR_FORCE_FALLBACK
       "Force software fallback rows for the native PVR pixel gate" OFF)
option(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE
       "Force one OBJ-free 8-row software band for mixed PVR ownership capture" OFF)
option(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG
       "Report applied PAL/VRAM/OAM writes by visible scanline and HBlank" OFF)
option(SMA2_NATIVE_PVR_INFLIGHT_DIAG
       "Enable source-pinned mGBADC PVR in-flight resource lifetime counters" OFF)
option(SMA2_NATIVE_PVR_COST_DIAG
       "Accumulate lean SMA2 native-PVR CPU cost buckets for reporting after benchmark close" OFF)
option(SMA2_PPU_COMPOSITOR_COST_DIAG
       "Time software scanline composition in the prepared Dreamcast GBARecomp overlay" OFF)
option(SMA2_PPU_SCANLINE_COST_DIAG
       "Time the GbaPpu::render_scanline body in the prepared Dreamcast PPU" OFF)
set(SMA2_NATIVE_PVR_DIAG_STOP_FRAMES "0" CACHE STRING
    "Stop a native-PVR in-flight diagnostic after N submitted frames (0 disables)")
set(SMA2_MGBADC_ROOT "/tmp/sma2-bg0-mgbadc-source" CACHE PATH
    "Source-pinned mGBADC checkout used by the isolated SMA2 PVR bridge")
if(SMA2_NATIVE_PVR_GBA_RENDER AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_NATIVE_PVR_GBA_RENDER requires SMA2_DREAMCAST=ON")
endif()
if(SMA2_NATIVE_PVR_SKIP_SOFTWARE AND NOT SMA2_NATIVE_PVR_GBA_RENDER)
    message(FATAL_ERROR "SMA2_NATIVE_PVR_SKIP_SOFTWARE requires SMA2_NATIVE_PVR_GBA_RENDER=ON")
endif()
if(SMA2_NATIVE_PVR_FRONT_CAPTURE AND NOT SMA2_NATIVE_PVR_GBA_RENDER)
    message(FATAL_ERROR "SMA2_NATIVE_PVR_FRONT_CAPTURE requires SMA2_NATIVE_PVR_GBA_RENDER=ON")
endif()
if(NOT SMA2_NATIVE_PVR_CAPTURE_FRAMES MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SMA2_NATIVE_PVR_CAPTURE_FRAMES must be a positive integer")
endif()
if(NOT SMA2_NATIVE_PVR_CAPTURE_START_FRAME MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SMA2_NATIVE_PVR_CAPTURE_START_FRAME must be a positive integer")
endif()
if(SMA2_NATIVE_PVR_FORCE_FALLBACK AND NOT SMA2_NATIVE_PVR_GBA_RENDER)
    message(FATAL_ERROR "SMA2_NATIVE_PVR_FORCE_FALLBACK requires SMA2_NATIVE_PVR_GBA_RENDER=ON")
endif()
if(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE AND
   (NOT SMA2_NATIVE_PVR_GBA_RENDER OR NOT SMA2_NATIVE_PVR_FRONT_CAPTURE OR
    SMA2_NATIVE_PVR_SKIP_SOFTWARE OR SMA2_NATIVE_PVR_FORCE_FALLBACK))
    message(FATAL_ERROR "SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE requires native PVR + front capture, software composition ON, and global force fallback OFF")
endif()
if((SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG OR SMA2_NATIVE_PVR_INFLIGHT_DIAG OR
    SMA2_NATIVE_PVR_COST_DIAG) AND
   NOT SMA2_NATIVE_PVR_GBA_RENDER)
    message(FATAL_ERROR "Native PVR diagnostics require SMA2_NATIVE_PVR_GBA_RENDER=ON")
endif()
if(NOT SMA2_NATIVE_PVR_DIAG_STOP_FRAMES MATCHES "^(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "SMA2_NATIVE_PVR_DIAG_STOP_FRAMES must be 0 or a positive integer")
endif()
if(SMA2_NATIVE_PVR_DIAG_STOP_FRAMES GREATER 0 AND
   (NOT SMA2_NATIVE_PVR_INFLIGHT_DIAG OR SMA2_NATIVE_PVR_FRONT_CAPTURE))
    message(FATAL_ERROR "A diagnostic stop limit requires in-flight diagnostics and front capture OFF")
endif()
if(SMA2_NATIVE_PVR_GBA_RENDER AND SMA2_PVR_PRESENT)
    message(FATAL_ERROR "Native BG/OBJ PVR rendering and the software-frame PVR presenter are exclusive")
endif()
if(SMA2_NATIVE_PVR_COST_DIAG AND SMA2_NATIVE_PVR_FRONT_CAPTURE)
    message(FATAL_ERROR "Lean native-PVR cost diagnostics cannot be combined with framebuffer capture")
endif()
option(SMA2_RGB565_SIDECAR
       "Present completed frames from GBARecomp's latched RGB565 sidecar" OFF)
option(SMA2_RGB565_HOST_TESTS "Build host tests for RGB565 presenter staging" OFF)
option(SMA2_IWRAM_AOT_ISLAND
       "Normal-gameplay relocated-IWRAM AOT: force-interp runner + native dispatch only at the static IWRAM entries (needs SMA2_IWRAM_AOT)" OFF)
option(SMA2_IWRAM_AOT_ISLAND_SWITCH
       "Arm-C layout control: read /pc/sma2_iwram_island.txt at install; '0' disables native island execution in the same ELF" OFF)
option(SMA2_NORMAL_BENCH_PMU
       "Normal-gameplay benchmark: count one SH-4 PMU event (PRFC1, chosen by /pc/sma2_pmu.txt) over the timed window" OFF)
set(SMA2_NORMAL_BENCH_FRAMES "0" CACHE STRING
    "N>0: bounded normal-gameplay benchmark of N frames from the autoloaded checkpoint (Dreamcast; no H5/parity)")
option(SMA2_NORMAL_PATH_HOST_PROBE
       "Host-only: link the per-VBlank fingerprint probe (wraps runtime_tick; installs the IWRAM guard when SMA2_IWRAM_AOT)" OFF)
option(SMA2_PILOT_WINDOW_HOST_TESTS "Build host tests for the event-bounded pilot window state machine" OFF)
option(SMA2_SNAPSHOT_H5_LITE
       "H5 timed window without per-instruction accounting (needs SMA2_SNAPSHOT_H5_MIXER_EPISODES>0)" OFF)
option(SMA2_IWRAM_AOT
       "Relocated-IWRAM AOT: install GBARecomp's iwram_code_guard over the ROM-copied sound mixer at startup" OFF)
option(SMA2_NATIVE_PVR_BRIDGE_HOST_TESTS
       "Build host tests for SMA2 native PVR bridge state contracts" OFF)
if(SMA2_PVR_PRESENT AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_PVR_PRESENT requires SMA2_DREAMCAST=ON")
endif()
if(SMA2_RGB565_SIDECAR AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_RGB565_SIDECAR requires SMA2_DREAMCAST=ON")
endif()
if(SMA2_RGB565_SIDECAR AND SMA2_NATIVE_PVR_GBA_RENDER AND
   SMA2_NATIVE_PVR_SKIP_SOFTWARE)
    message(FATAL_ERROR
        "SMA2_RGB565_SIDECAR cannot be combined with native PVR software-scanline skipping: "
        "native_scanline() may bypass RGB565 sidecar population")
endif()
option(SMA2_DEV_CHECKPOINT "Build the developer Y-button checkpoint save path" OFF)
option(SMA2_DEV_CHECKPOINT_AUTOLOAD
       "Autoload /pc/sma2_checkpoint.bin at Dreamcast startup" OFF)
option(SMA2_MEMORY_ALLOC_DIAG
       "Record large newlib allocations and sbrk growth requests on Dreamcast" OFF)
option(SMA2_SNAPSHOT_INPUT_DIAG
       "Inject a bounded synthetic Y press sequence for checkpoint hardware validation" OFF)
option(SMA2_SNAPSHOT_H5_DIAG
       "Run an opt-in bounded interpreter snapshot differential on Dreamcast" OFF)
option(SMA2_AOT_MODE_PARITY_DIAG
       "Run H5 parity through static-dispatch interpreter misses on Dreamcast" OFF)
set(SMA2_SNAPSHOT_H5_INSTRUCTIONS "64" CACHE STRING
    "Retired guest instruction count for the Dreamcast snapshot differential")
set(SMA2_SNAPSHOT_H5_MIXER_EPISODES "0" CACHE STRING
    "0 = window ends after SMA2_SNAPSHOT_H5_INSTRUCTIONS retired instructions. N>0 = event-bounded window: ends at the first retired instruction outside the IWRAM mixer range after the Nth converter entry (0x03001518), i.e. after N completed mixer episodes")
option(SMA2_SNAPSHOT_TESTS "Build the SMA2 checkpoint host differential" OFF)
set(SMA2_SNAPSHOT_ROM
    "${CMAKE_CURRENT_SOURCE_DIR}/roms/Super Mario Advance 2 - Super Mario World (USA).gba"
    CACHE FILEPATH "ROM used by the SMA2 snapshot differential")
set(SMA2_SNAPSHOT_BIOS "${CMAKE_CURRENT_SOURCE_DIR}/roms/gba_bios.bin"
    CACHE FILEPATH "BIOS used by the SMA2 snapshot differential")
option(SMA2_SH4_DYNAREC "Link the vendored Bruce mGBA ARM/Thumb -> SH-4 dynarec CPU backend" OFF)
option(SMA2_SH4_AOT_POC "Route the opt-in precompiled Stage B block through GBARecomp dispatch" OFF)
option(SMA2_AOT_TAIL_PLACEMENT
       "Keep the two SMA2 diagnostic AOT functions in separate tail text sections" OFF)
option(SMA2_LOW_PMU
       "Link the low-perturbation window PMU wrapper (inert unless an offline patch redirects the H5 timing call)" OFF)
option(SMA2_B16_GAP
       "Link an inert, never-executed 13,664-byte .text gap (insertion-only I-cache layout control)" OFF)
option(SMA2_BUILD_AOT_CODEGEN "Build the native-host SMA2 AOT source generator" OFF)
option(SMA2_CART_AOT_DIAG "Run opt-in generated-cart dispatch and event differentials, then exit" OFF)
option(SMA2_CART_AOT_DIAG_ISOLATED_ONLY
       "Run only the previously hardware-validated isolated generated leaf differential" OFF)
option(SMA2_CART_AOT_DIAG_RESULT_HOST_TESTS
       "Build host process-status tests for bounded AOT diagnostic results" OFF)
option(SMA2_CART_AOT_IRQ_DIAG "Add an opt-in real-startup eligible IRQ continuation differential" OFF)
option(SMA2_CART_AOT_BENCH "Measure the bounded generated-cart fixture and helper calls" OFF)
set(SMA2_CART_AOT_OPT "1" CACHE STRING "Optimization level for generated cart sources: 0, 1, 2, 3, or s")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the shared recomp-ui checkout")
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
    CACHE PATH "recomp-ui root used by the live GBA runtime adapter")
if(SMA2_DREAMCAST)
    set(GBARECOMP_ENABLE_MODS OFF CACHE BOOL
        "Enable Super Mario World's built-in mod catalog" FORCE)
else()
    set(GBARECOMP_ENABLE_MODS ON CACHE BOOL
        "Enable Super Mario World's built-in mod catalog" FORCE)
endif()
if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT is not a gbarecomp checkout:\n"
        "  ${GBARECOMP_ROOT}\n"
        "Configure with -DGBARECOMP_ROOT=<path> if the local worktree link is absent.")
endif()
if(SMA2_RGB565_SIDECAR)
    file(READ "${GBARECOMP_ROOT}/src/gba/gba_ppu.h" SMA2_PPU_HEADER)
    foreach(SIDECAR_API
            "set_rgb565_sidecar_enabled(bool enabled)"
            "latched_framebuffer_rgb565() const"
            "present_rgb565(const uint16_t* rgb565)")
        string(FIND "${SMA2_PPU_HEADER}" "${SIDECAR_API}" SIDECAR_API_POS)
        if(SIDECAR_API STREQUAL "present_rgb565(const uint16_t* rgb565)")
            file(READ "${GBARECOMP_ROOT}/src/runtime/host_window.h" SMA2_HOST_WINDOW_HEADER)
            string(FIND "${SMA2_HOST_WINDOW_HEADER}" "${SIDECAR_API}" SIDECAR_API_POS)
        endif()
        if(SIDECAR_API_POS EQUAL -1)
            message(FATAL_ERROR
                "SMA2_RGB565_SIDECAR requires the validated GBARecomp API '${SIDECAR_API}'. "
                "Selected GBARECOMP_ROOT=${GBARECOMP_ROOT}. "
                "Use the validated fork explicitly; do not initialize or copy the submodule.")
        endif()
    endforeach()
    set(SMA2_RGB565_VALIDATED_GBA_HASHES
        "src/gba/gba_ppu.h|638e35559f4de833e790bcc3944a15d3a6198ca211a093e6f02909bdcdfb11dc"
        "src/gba/gba_ppu.cpp|d35bc07858f23d1ad280aab81b7f6ec44c8e15cec31311b4035575a3ba0786b3"
        "src/gba/gba_simulation_state.cpp|20fd118674440cca5de513fbf3bb632cc8f5f9326696e05015e3a23a29aa4527")
    foreach(HASH_ENTRY IN LISTS SMA2_RGB565_VALIDATED_GBA_HASHES)
        string(REPLACE "|" ";" HASH_PARTS "${HASH_ENTRY}")
        list(GET HASH_PARTS 0 RELATIVE_PATH)
        list(GET HASH_PARTS 1 EXPECTED_SHA256)
        file(SHA256 "${SMA2_RGB565_SOURCE_ROOT}/${RELATIVE_PATH}" ACTUAL_SHA256)
        if(NOT ACTUAL_SHA256 STREQUAL EXPECTED_SHA256)
            message(FATAL_ERROR
                "Selected GBARecomp RGB565 source is not the host-validated input: ${RELATIVE_PATH}. "
                "Expected ${EXPECTED_SHA256}, got ${ACTUAL_SHA256}. "
                "Review and revalidate the framework change before updating this pin.")
        endif()
    endforeach()
endif()

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

if(SMA2_BUILD_AOT_CODEGEN)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "SMA2_BUILD_AOT_CODEGEN must use a native host compiler")
    endif()
    add_executable(sma2_aot_codegen tools/sma2_aot_codegen.cpp)
    target_link_libraries(sma2_aot_codegen PRIVATE gbarecomp_recompile_core)
endif()

if(EXISTS "${SMA2_GENERATED_DIR}/recompiled.cpp")
    message(FATAL_ERROR
        "Stale monolithic ${SMA2_GENERATED_DIR}/recompiled.cpp found. "
        "Run tools/regen.ps1 with the current gbarecomp worktree.")
endif()
file(GLOB GAME_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${SMA2_GENERATED_DIR}/*.c"
    "${SMA2_GENERATED_DIR}/*.cpp")
if(SMA2_REQUIRE_GENERATED_DISPATCH)
    set(_sma2_dispatch_table "${SMA2_GENERATED_DIR}/dispatch_table.cpp")
    if(NOT EXISTS "${_sma2_dispatch_table}")
        message(FATAL_ERROR
            "SMA2_REQUIRE_GENERATED_DISPATCH is ON but ${_sma2_dispatch_table} is missing")
    endif()
    file(READ "${_sma2_dispatch_table}" _sma2_dispatch_source)
    string(REGEX MATCHALL
        "\\{0x[0-9A-Fa-f]+u, [01]u, [01]u, [A-Za-z_][A-Za-z0-9_]*\\},"
        _sma2_dispatch_rows "${_sma2_dispatch_source}")
    list(LENGTH _sma2_dispatch_rows _sma2_dispatch_count)
    string(REGEX MATCH
        "kDispatchTableLen[ \t]*=[ \t]*([0-9]+)"
        _sma2_dispatch_length_match "${_sma2_dispatch_source}")
    set(_sma2_dispatch_declared_count "${CMAKE_MATCH_1}")
    if(_sma2_dispatch_count LESS 1)
        message(FATAL_ERROR
            "SMA2_REQUIRE_GENERATED_DISPATCH is ON but dispatch_table.cpp has no function entries")
    endif()
    if(NOT _sma2_dispatch_length_match OR
       _sma2_dispatch_declared_count LESS 1 OR
       NOT _sma2_dispatch_declared_count STREQUAL "${_sma2_dispatch_count}")
        message(FATAL_ERROR
            "SMA2_REQUIRE_GENERATED_DISPATCH found ${_sma2_dispatch_count} rows but "
            "dispatch_table.cpp declares kDispatchTableLen=${_sma2_dispatch_declared_count}")
    endif()
    message(STATUS "Required generated SMA2 dispatch entries: ${_sma2_dispatch_count}")
endif()

add_executable(SuperMarioWorldRecomp
    src/main.cpp
    src/smw_extended_view.cpp
    ${GAME_GENERATED_SOURCES})
if(SMA2_CART_AOT_DIAG)
    if(NOT EXISTS "${SMA2_GENERATED_DIR}/dispatch_table.cpp")
        message(FATAL_ERROR "SMA2_CART_AOT_DIAG requires a generated cart corpus")
    endif()
    if(SMA2_SH4_AOT_POC OR SMA2_SH4_DYNAREC)
        message(FATAL_ERROR "SMA2_CART_AOT_DIAG is isolated from Stage B and runtime JIT paths")
    endif()
    target_sources(SuperMarioWorldRecomp PRIVATE src/sma2_corpus_dispatch_diag.cpp)
    target_include_directories(gbarecomp_runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_CART_AOT_DIAG=1)
    target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_CART_AOT_DIAG=1)
    target_link_options(SuperMarioWorldRecomp PRIVATE "-Wl,--wrap=runtime_tick")
endif()
if(SMA2_CART_AOT_DIAG_ISOLATED_ONLY)
    if(NOT SMA2_CART_AOT_DIAG)
        message(FATAL_ERROR "SMA2_CART_AOT_DIAG_ISOLATED_ONLY requires SMA2_CART_AOT_DIAG=ON")
    endif()
    if(SMA2_CART_AOT_IRQ_DIAG OR SMA2_CART_AOT_BENCH)
        message(FATAL_ERROR "SMA2_CART_AOT_DIAG_ISOLATED_ONLY cannot include IRQ or benchmark fixtures")
    endif()
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_CART_AOT_DIAG_ISOLATED_ONLY=1)
endif()
if(SMA2_CART_AOT_BENCH)
    if(NOT SMA2_CART_AOT_DIAG)
        message(FATAL_ERROR "SMA2_CART_AOT_BENCH requires SMA2_CART_AOT_DIAG=ON")
    endif()
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_CART_AOT_BENCH=1)
    target_link_options(SuperMarioWorldRecomp PRIVATE
        "-Wl,--wrap=runtime_should_yield")
endif()
if(SMA2_CART_AOT_IRQ_DIAG)
    if(NOT SMA2_CART_AOT_DIAG)
        message(FATAL_ERROR "SMA2_CART_AOT_IRQ_DIAG requires SMA2_CART_AOT_DIAG=ON")
    endif()
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_CART_AOT_IRQ_DIAG=1)
    target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_CART_AOT_IRQ_DIAG=1)
    target_link_options(SuperMarioWorldRecomp PRIVATE
        "-Wl,--wrap=runtime_irq"
        "-Wl,--wrap=runtime_should_yield")
endif()
if(NOT SMA2_DREAMCAST)
    target_sources(SuperMarioWorldRecomp PRIVATE src/mods/smw_adaptive_view_plugin.cpp)
endif()

set(GAME_RECOMPILED_SOURCES ${GAME_GENERATED_SOURCES})
list(FILTER GAME_RECOMPILED_SOURCES INCLUDE REGEX
    "/recompiled_[0-9]+\\.cpp$")
if(GAME_RECOMPILED_SOURCES)
    if(NOT SMA2_CART_AOT_OPT MATCHES "^[0123s]$")
        message(FATAL_ERROR "SMA2_CART_AOT_OPT must be one of 0, 1, 2, 3, or s")
    endif()
    set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-O${SMA2_CART_AOT_OPT};-g0;-Wno-unused-variable;-Wno-unused-but-set-variable")
endif()

if(SMA2_NORMAL_PATH_HOST_PROBE)
    if(SMA2_DREAMCAST)
        message(FATAL_ERROR "SMA2_NORMAL_PATH_HOST_PROBE is host-only")
    endif()
    target_sources(SuperMarioWorldRecomp PRIVATE tools/sma2_normal_path_probe.cpp)
    target_include_directories(SuperMarioWorldRecomp PRIVATE src)
    target_link_options(SuperMarioWorldRecomp PRIVATE "-Wl,--wrap=runtime_tick")
endif()
if(SMA2_NORMAL_BENCH_FRAMES GREATER 0)
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_NORMAL_BENCH_FRAMES=${SMA2_NORMAL_BENCH_FRAMES})
    target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_NORMAL_BENCH_FRAMES=${SMA2_NORMAL_BENCH_FRAMES})
    if(SMA2_NORMAL_BENCH_PMU)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_NORMAL_BENCH_PMU=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_NORMAL_BENCH_PMU=1)
    endif()
endif()
if(SMA2_NORMAL_BENCH_PMU AND NOT SMA2_NORMAL_BENCH_FRAMES GREATER 0)
    message(FATAL_ERROR "SMA2_NORMAL_BENCH_PMU requires SMA2_NORMAL_BENCH_FRAMES > 0")
endif()
if(SMA2_IWRAM_AOT)
    target_sources(SuperMarioWorldRecomp PRIVATE src/sma2_iwram_aot.cpp)
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_IWRAM_AOT=1)
    # dreamcast_host_window.inc (which calls sma2::iwram_aot_install) is compiled INSIDE gbarecomp_runtime
    # (host_window.cpp), so the definition and the header path must reach that library too.
    target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_IWRAM_AOT=1)
    if(SMA2_IWRAM_AOT_ISLAND)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_IWRAM_AOT_ISLAND=1)
        if(SMA2_IWRAM_AOT_ISLAND_SWITCH)
            target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_IWRAM_AOT_ISLAND_SWITCH=1)
        endif()
        target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_IWRAM_AOT_ISLAND=1)
    endif()
    target_include_directories(gbarecomp_runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

target_include_directories(SuperMarioWorldRecomp PRIVATE
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug")

target_link_libraries(SuperMarioWorldRecomp PRIVATE
    "-Wl,--start-group"
    gbarecomp_runtime
    gbarecomp_debug
    gbarecomp_gba
    gbarecomp_armv4t
    "-Wl,--end-group")

target_compile_definitions(SuperMarioWorldRecomp PRIVATE
    GBARECOMP_DEFAULT_DEBUG_PORT=19877
    GBARECOMP_WINDOW_TITLE="SuperMarioWorldRecomp"
    GBARECOMP_DEFAULT_GAME_CONFIG="game.toml")

if(SMA2_SH4_DYNAREC AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR
        "SMA2_SH4_DYNAREC is a Dreamcast-target backend; it emits SH-4 code.")
endif()
if(SMA2_SH4_AOT_POC AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_SH4_AOT_POC is a Dreamcast-target diagnostic")
endif()
if(SMA2_IWRAM_AOT AND SMA2_SH4_DYNAREC)
    message(FATAL_ERROR
        "SMA2_IWRAM_AOT is incompatible with SMA2_SH4_DYNAREC: the dynarec is handed the raw IWRAM array and "
        "bypasses the IWRAM code-watch store barrier. Disable one of them; the build will not do it silently.")
endif()
if(SMA2_IWRAM_AOT_ISLAND_SWITCH AND NOT SMA2_IWRAM_AOT_ISLAND)
    message(FATAL_ERROR "SMA2_IWRAM_AOT_ISLAND_SWITCH requires SMA2_IWRAM_AOT_ISLAND=ON")
endif()
if(SMA2_IWRAM_AOT_ISLAND AND NOT SMA2_IWRAM_AOT)
    message(FATAL_ERROR "SMA2_IWRAM_AOT_ISLAND requires SMA2_IWRAM_AOT=ON")
endif()
if(SMA2_IWRAM_AOT_ISLAND AND (SMA2_AOT_MODE_PARITY_DIAG OR SMA2_SNAPSHOT_H5_DIAG))
    message(FATAL_ERROR "SMA2_IWRAM_AOT_ISLAND is the normal-gameplay mode; it is exclusive with the H5/parity diagnostics")
endif()
if(NOT SMA2_NORMAL_BENCH_FRAMES MATCHES "^([0-9]|[1-5][0-9]|6[0-4])$")
    message(FATAL_ERROR "SMA2_NORMAL_BENCH_FRAMES must be an integer 0..64")
endif()
if(SMA2_NORMAL_BENCH_FRAMES GREATER 0)
    if(NOT SMA2_DREAMCAST OR NOT SMA2_DEV_CHECKPOINT_AUTOLOAD)
        message(FATAL_ERROR "SMA2_NORMAL_BENCH_FRAMES requires SMA2_DREAMCAST and SMA2_DEV_CHECKPOINT_AUTOLOAD")
    endif()
    if(SMA2_SNAPSHOT_H5_DIAG OR SMA2_SNAPSHOT_INPUT_DIAG OR SMA2_MEMORY_ALLOC_DIAG OR SMA2_AOT_MODE_PARITY_DIAG OR SMA2_CART_AOT_DIAG)
        message(FATAL_ERROR "SMA2_NORMAL_BENCH_FRAMES is exclusive with the H5/input/alloc/parity/cart diagnostics")
    endif()
endif()
if(SMA2_IWRAM_AOT AND SMA2_CART_AOT_DIAG)
    message(FATAL_ERROR
        "SMA2_IWRAM_AOT cannot be combined with SMA2_CART_AOT_DIAG (its diagnostics hold long-lived writable IWRAM pointers)")
endif()
if(SMA2_IWRAM_AOT AND NOT GBARECOMP_IWRAM_CODE_WATCH)
    message(WARNING
        "SMA2_IWRAM_AOT with GBARECOMP_IWRAM_CODE_WATCH=OFF: store barrier compiled out. DIAGNOSTIC PERFORMANCE "
        "CEILING ONLY - not valid for gameplay or correctness acceptance.")
endif()
if(SMA2_SH4_AOT_POC AND SMA2_SH4_DYNAREC)
    message(FATAL_ERROR
        "SMA2_SH4_AOT_POC validates GBARecomp dispatch; disable the separate runtime dynarec")
endif()
if(SMA2_DEV_CHECKPOINT AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_DEV_CHECKPOINT requires SMA2_DREAMCAST=ON")
endif()
if(SMA2_DEV_CHECKPOINT_AUTOLOAD AND NOT SMA2_DEV_CHECKPOINT)
    message(FATAL_ERROR
        "SMA2_DEV_CHECKPOINT_AUTOLOAD requires SMA2_DEV_CHECKPOINT=ON")
endif()
if(SMA2_DEV_CHECKPOINT_AUTOLOAD AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_DEV_CHECKPOINT_AUTOLOAD is Dreamcast-only")
endif()
if(SMA2_MEMORY_ALLOC_DIAG AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_MEMORY_ALLOC_DIAG is Dreamcast-only")
endif()
if(SMA2_SNAPSHOT_INPUT_DIAG AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_SNAPSHOT_INPUT_DIAG is Dreamcast-only")
endif()
if(SMA2_SNAPSHOT_INPUT_DIAG AND NOT SMA2_DEV_CHECKPOINT)
    message(FATAL_ERROR "SMA2_SNAPSHOT_INPUT_DIAG requires SMA2_DEV_CHECKPOINT=ON")
endif()
if(SMA2_SNAPSHOT_INPUT_DIAG AND SMA2_DEV_CHECKPOINT_AUTOLOAD)
    message(FATAL_ERROR "SMA2_SNAPSHOT_INPUT_DIAG requires autoload OFF")
endif()
if(SMA2_SNAPSHOT_INPUT_DIAG AND SMA2_MEMORY_ALLOC_DIAG)
    message(FATAL_ERROR "SMA2_SNAPSHOT_INPUT_DIAG cannot be combined with the allocator probe")
endif()
if(SMA2_SNAPSHOT_H5_DIAG AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_DIAG is Dreamcast-only")
endif()
if(SMA2_SNAPSHOT_H5_DIAG AND NOT SMA2_DEV_CHECKPOINT)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_DIAG requires SMA2_DEV_CHECKPOINT=ON")
endif()
if(SMA2_SNAPSHOT_H5_DIAG AND SMA2_SNAPSHOT_INPUT_DIAG)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_DIAG replaces SMA2_SNAPSHOT_INPUT_DIAG")
endif()
if(SMA2_SNAPSHOT_H5_DIAG AND SMA2_MEMORY_ALLOC_DIAG)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_DIAG cannot be combined with the allocator probe")
endif()
if(NOT SMA2_SNAPSHOT_H5_MIXER_EPISODES MATCHES "^([0-9]|1[0-6])$")
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_MIXER_EPISODES must be an integer 0..16")
endif()
if(SMA2_SNAPSHOT_H5_LITE AND NOT SMA2_SNAPSHOT_H5_MIXER_EPISODES GREATER 0)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_LITE requires SMA2_SNAPSHOT_H5_MIXER_EPISODES > 0")
endif()
if(SMA2_SNAPSHOT_H5_LITE AND NOT SMA2_SNAPSHOT_H5_DIAG)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_LITE requires SMA2_SNAPSHOT_H5_DIAG=ON")
endif()
if(SMA2_SNAPSHOT_H5_MIXER_EPISODES GREATER 0 AND NOT SMA2_SNAPSHOT_H5_DIAG)
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_MIXER_EPISODES requires SMA2_SNAPSHOT_H5_DIAG=ON")
endif()
if(NOT SMA2_SNAPSHOT_H5_INSTRUCTIONS MATCHES "^(1|16|64|256|1024|4096|16384|65536)$")
    message(FATAL_ERROR "SMA2_SNAPSHOT_H5_INSTRUCTIONS must be 1, 16, 64, 256, 1024, 4096, 16384, or 65536")
endif()
if(SMA2_AOT_MODE_PARITY_DIAG AND NOT SMA2_DREAMCAST)
    message(FATAL_ERROR "SMA2_AOT_MODE_PARITY_DIAG is Dreamcast-only")
endif()
if(SMA2_AOT_MODE_PARITY_DIAG AND NOT SMA2_SNAPSHOT_H5_DIAG)
    message(FATAL_ERROR "SMA2_AOT_MODE_PARITY_DIAG requires SMA2_SNAPSHOT_H5_DIAG=ON")
endif()
if(SMA2_AOT_MODE_PARITY_DIAG AND NOT SMA2_DEV_CHECKPOINT_AUTOLOAD)
    message(FATAL_ERROR "SMA2_AOT_MODE_PARITY_DIAG requires checkpoint autoload")
endif()
if(SMA2_DREAMCAST)
    if(NOT KOS_ARCH STREQUAL "dreamcast")
        message(FATAL_ERROR "SMA2_DREAMCAST requires KallistiOS kos-cmake")
    endif()
    if(SMA2_SH4_DYNAREC)
        # Vendored Bruce mGBA ARM/Thumb -> SH-4 dynarec (see
        # vendor/mgba-sh4-jit/PROVENANCE.md). jit-host.c is the off-target host
        # backend (it runs generated SH-4 under sh4-interp.h on a 32-bit host);
        # it is NOT part of the Dreamcast link, and fastmem.c is only built for
        # the JIT_FASTMEM ROM-paging model, which this integration does not use.
        file(GLOB SMA2_JIT_VENDORED_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mgba-sh4-jit/src/arm/jit/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mgba-sh4-jit/src/arm/*.c")
        list(REMOVE_ITEM SMA2_JIT_VENDORED_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mgba-sh4-jit/src/arm/jit/jit-host.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mgba-sh4-jit/src/arm/jit/fastmem.c")
        target_sources(SuperMarioWorldRecomp PRIVATE
            src/sh4jit/sma2_sh4_backend.cpp ${SMA2_JIT_VENDORED_SOURCES})
        set_source_files_properties(${SMA2_JIT_VENDORED_SOURCES}
            src/sh4jit/sma2_sh4_backend.cpp PROPERTIES
            COMPILE_DEFINITIONS "M_ARM_JIT;_GNU_SOURCE;SMA2_JIT_SLOWMEM;SMA2_JIT_ROM_ONLY")
        target_include_directories(SuperMarioWorldRecomp PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mgba-sh4-jit/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mgba-sh4-jit")
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_SH4_DYNAREC=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_SH4_DYNAREC=1)
    endif()
    if(SMA2_SH4_AOT_POC)
        set(SMA2_AOT_GENERATED_SOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/sma2_aot_stage_b.generated.cpp")
        if(NOT EXISTS "${SMA2_AOT_GENERATED_SOURCE}")
            message(FATAL_ERROR
                "Missing ${SMA2_AOT_GENERATED_SOURCE}; run tools/build_dreamcast.sh "
                "with SMA2_SH4_AOT_POC=ON to generate the host artifact first")
        endif()
        target_sources(SuperMarioWorldRecomp PRIVATE
            src/sh4jit/sma2_sh4_aot.cpp "${SMA2_AOT_GENERATED_SOURCE}")
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_SH4_AOT_POC=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_SH4_AOT_POC=1)
        target_compile_definitions(gbarecomp_armv4t PRIVATE
            SMA2_SH4_AOT_POC=1)
        target_link_options(SuperMarioWorldRecomp PRIVATE
            "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/SuperMarioWorldRecomp.map")
    endif()
    target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_DREAMCAST=1)
    if(SMA2_NATIVE_PVR_GBA_RENDER)
        if(NOT SMA2_DEV_CHECKPOINT)
            message(FATAL_ERROR "SMA2_NATIVE_PVR_GBA_RENDER requires SMA2_DEV_CHECKPOINT=ON for checkpoint lifecycle hooks")
        endif()
        if(NOT EXISTS "${SMA2_MGBADC_ROOT}/CMakeLists.txt")
            message(FATAL_ERROR "Source-pinned mGBADC tree not found: ${SMA2_MGBADC_ROOT}")
        endif()
        set(LIBMGBA_ONLY OFF CACHE BOOL "Build core used by SMA2 native PVR bridge" FORCE)
        set(MGBA_DC_NATIVE_FRONTEND OFF CACHE BOOL "Disable mGBADC frontend" FORCE)
        set(MGBA_DC_PVR_GBA_RENDER ON CACHE BOOL "Use existing native GBA PVR renderer" FORCE)
        set(MGBA_DC_PVR_GBA_INFLIGHT_DIAG ${SMA2_NATIVE_PVR_INFLIGHT_DIAG}
            CACHE BOOL "Enable mGBADC in-flight diagnostic for SMA2" FORCE)
        set(USE_ELF OFF CACHE BOOL "Disable unused mGBA ELF loading" FORCE)
        set(USE_DEBUGGERS OFF CACHE BOOL "Disable unused mGBA debugger" FORCE)
        set(USE_GDB_STUB OFF CACHE BOOL "Disable unused mGBA GDB stub" FORCE)
        set(USE_DISCORD_RPC OFF CACHE BOOL "Disable unused mGBA Discord" FORCE)
        set(USE_SQLITE3 OFF CACHE BOOL "Disable unused mGBA database" FORCE)
        set(USE_LZMA OFF CACHE BOOL "Disable unused mGBA LZMA" FORCE)
        set(USE_ZLIB OFF CACHE BOOL "Disable unused mGBA zlib" FORCE)
        set(USE_MINIZIP OFF CACHE BOOL "Disable unused mGBA minizip" FORCE)
        set(USE_LIBZIP OFF CACHE BOOL "Disable unused mGBA libzip" FORCE)
        set(USE_PNG OFF CACHE BOOL "Disable unused mGBA PNG" FORCE)
        set(USE_EDITLINE OFF CACHE BOOL "Disable unused mGBA editline" FORCE)
        set(USE_EPOXY OFF CACHE BOOL "Disable unused mGBA epoxy" FORCE)
        set(BUILD_TEST OFF CACHE BOOL "Disable mGBA tests" FORCE)
        set(BUILD_CINEMA OFF CACHE BOOL "Disable mGBA cinema" FORCE)
        set(BUILD_DOCGEN OFF CACHE BOOL "Disable mGBA docgen" FORCE)
        set(ENABLE_SCRIPTING OFF CACHE BOOL "Disable mGBA scripting" FORCE)
        set(BUILD_GL OFF CACHE BOOL "Disable mGBA GL" FORCE)
        set(BUILD_GLES2 OFF CACHE BOOL "Disable mGBA GLES2" FORCE)
        set(BUILD_GLES3 OFF CACHE BOOL "Disable mGBA GLES3" FORCE)
        add_subdirectory("${SMA2_MGBADC_ROOT}" sma2_mgbadc EXCLUDE_FROM_ALL)
        target_sources(gbarecomp_runtime PRIVATE
            src/dreamcast_native_pvr_bridge.cpp
            src/dreamcast_native_pvr_bridge_hook.c
            "${SMA2_MGBADC_ROOT}/src/platform/dreamcast/native/dc-pvr-gba.c")
        target_include_directories(gbarecomp_runtime PRIVATE
            "${SMA2_MGBADC_ROOT}/include"
            "${SMA2_MGBADC_ROOT}/src/platform/dreamcast/native")
        target_compile_definitions(gbarecomp_runtime PRIVATE
            MGBA_DC_PVR_GBA_RENDER=1 SMA2_NATIVE_PVR_GBA_RENDER=1)
        if(SMA2_NATIVE_PVR_DIAG_STOP_FRAMES GREATER 0)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_DIAG_STOP_FRAMES=${SMA2_NATIVE_PVR_DIAG_STOP_FRAMES})
        endif()
        if(SMA2_NATIVE_PVR_INFLIGHT_DIAG)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                MGBA_DC_PVR_GBA_INFLIGHT_DIAG=1)
        endif()
        if(SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG=1)
        endif()
        if(SMA2_NATIVE_PVR_COST_DIAG)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_COST_DIAG=1)
        endif()
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_NATIVE_PVR_GBA_RENDER=1)
        if(SMA2_NATIVE_PVR_SKIP_SOFTWARE)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_SKIP_SOFTWARE=1)
        endif()
        if(SMA2_NATIVE_PVR_FORCE_FALLBACK)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_FORCE_FALLBACK=1)
        endif()
        if(SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE=1)
        endif()
        if(SMA2_NATIVE_PVR_FRONT_CAPTURE)
            target_compile_definitions(gbarecomp_runtime PRIVATE
                SMA2_NATIVE_PVR_FRONT_CAPTURE=1
                SMA2_NATIVE_PVR_CAPTURE_START_FRAME=${SMA2_NATIVE_PVR_CAPTURE_START_FRAME}
                SMA2_NATIVE_PVR_CAPTURE_FRAMES=${SMA2_NATIVE_PVR_CAPTURE_FRAMES})
        endif()
        target_compile_definitions(mgba PRIVATE
            _POSIX_C_SOURCE=200809L HAVE_LOCALE HAVE_STRDUP HAVE_STRTOF_L)
        target_compile_options(mgba PRIVATE
            "-include${CMAKE_CURRENT_SOURCE_DIR}/tools/sma2_pvr_bg0_fixture/kos_mgba_compat.h")
        target_link_libraries(SuperMarioWorldRecomp PRIVATE mgba m)
    endif()
    if(SMA2_PVR_PRESENT)
        target_sources(SuperMarioWorldRecomp PRIVATE src/dreamcast_pvr_presenter.cpp)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_PVR_PRESENT=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_PVR_PRESENT=1)
    endif()
    if(NOT EXISTS "${SMA2_GENERATED_DIR}/dispatch_table.cpp")
        target_sources(SuperMarioWorldRecomp PRIVATE
            src/dreamcast_empty_dispatch.cpp)
        message(STATUS
            "No cart dispatch_table.cpp at ${SMA2_GENERATED_DIR}; linking empty dispatch")
    else()
        message(STATUS
            "Linking GBARecomp cart corpus from ${SMA2_GENERATED_DIR}")
    endif()
    target_compile_definitions(gbarecomp_runtime PRIVATE SMA2_DREAMCAST=1)
    if(SMA2_PPU_COMPOSITOR_COST_DIAG)
        target_compile_definitions(gbarecomp_gba PRIVATE
            SMA2_PPU_COMPOSITOR_COST_DIAG=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_PPU_COMPOSITOR_COST_DIAG=1)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_PPU_COMPOSITOR_COST_DIAG=1)
    endif()
    if(SMA2_PPU_SCANLINE_COST_DIAG)
        target_compile_definitions(gbarecomp_gba PRIVATE
            SMA2_PPU_SCANLINE_COST_DIAG=1)
    endif()
    if(SMA2_RGB565_SIDECAR)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_RGB565_SIDECAR=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_RGB565_SIDECAR=1)
    endif()
    # The always-on audio capture ring is diagnostic output, not emulated
    # mixer/playback state. Keep it for explicit AOT instrumentation builds.
    if(NOT SMA2_CART_AOT_DIAG)
        target_compile_definitions(gbarecomp_gba PRIVATE
            SMA2_DC_DISABLE_AUDIO_CAPTURE_RING=1)
    endif()
    target_include_directories(gbarecomp_runtime PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(SMA2_DEV_CHECKPOINT)
        target_sources(SuperMarioWorldRecomp PRIVATE
            src/snapshots/sma2_checkpoint.cpp)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_DEV_CHECKPOINT=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_DEV_CHECKPOINT=1)
    endif()
    if(SMA2_DEV_CHECKPOINT_AUTOLOAD)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_DEV_CHECKPOINT_AUTOLOAD=1)
    endif()
    target_link_options(SuperMarioWorldRecomp PRIVATE
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/SuperMarioWorldRecomp.map")
    if(SMA2_AOT_TAIL_PLACEMENT)
        if(NOT EXISTS "${SMA2_GENERATED_DIR}/dispatch_table.cpp")
            message(FATAL_ERROR "SMA2_AOT_TAIL_PLACEMENT requires the generated dispatch table")
        endif()
        target_link_options(SuperMarioWorldRecomp PRIVATE
            "-Wl,--unique=.text.gf_tfunc_080740EC"
            "-Wl,--unique=.text.gf_tfunc_0809B4C8")
    endif()
    if(SMA2_B16_GAP)
        # Linked as the FIRST object so its .text lands ahead of the LTO partitions
        # (non-LTO objects listed after the first IR object land after all ltrans output).
        add_library(sma2_gap_pad OBJECT src/sma2_gap_pad.cpp)
        target_compile_options(sma2_gap_pad PRIVATE
            -fno-lto -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables)
        add_dependencies(SuperMarioWorldRecomp sma2_gap_pad)
        target_link_options(SuperMarioWorldRecomp PRIVATE
            $<TARGET_OBJECTS:sma2_gap_pad> "-Wl,--undefined=_sma2_gap_pad")
    endif()
    if(SMA2_LOW_PMU)
        target_sources(SuperMarioWorldRecomp PRIVATE src/snapshots/sma2_window_pmu.cpp)
        set_source_files_properties(src/snapshots/sma2_window_pmu.cpp PROPERTIES
            COMPILE_OPTIONS "-fno-lto;-fno-exceptions;-fno-unwind-tables;-fno-asynchronous-unwind-tables")
        # GC root only: nothing calls the wrapper until an offline patch redirects the H5
        # timing literal, so keep its section from being discarded.
        target_link_options(SuperMarioWorldRecomp PRIVATE
            "-Wl,--undefined=_sma2pmu_get_ticks")
    endif()
    if(SMA2_MEMORY_ALLOC_DIAG)
        target_sources(SuperMarioWorldRecomp PRIVATE src/sma2_memory_alloc_diag.cpp)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_MEMORY_ALLOC_DIAG=1)
        target_link_options(SuperMarioWorldRecomp PRIVATE
            "-Wl,--wrap=malloc"
            "-Wl,--wrap=calloc"
            "-Wl,--wrap=realloc"
            "-Wl,--wrap=_Znwj"
            "-Wl,--wrap=_malloc_r"
            "-Wl,--wrap=_calloc_r"
            "-Wl,--wrap=_realloc_r"
            "-Wl,--wrap=_sbrk_r")
    endif()
    if(SMA2_SNAPSHOT_INPUT_DIAG)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_SNAPSHOT_INPUT_DIAG=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_SNAPSHOT_INPUT_DIAG=1)
    endif()
    if(SMA2_SNAPSHOT_H5_DIAG)
        target_sources(SuperMarioWorldRecomp PRIVATE
            src/snapshots/sma2_snapshot_h5_diag.cpp)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_SNAPSHOT_H5_DIAG=1
            SMA2_SNAPSHOT_H5_INSTRUCTIONS=${SMA2_SNAPSHOT_H5_INSTRUCTIONS}
            SMA2_SNAPSHOT_H5_MIXER_EPISODES=${SMA2_SNAPSHOT_H5_MIXER_EPISODES})
        if(SMA2_SNAPSHOT_H5_LITE)
            target_compile_definitions(SuperMarioWorldRecomp PRIVATE SMA2_SNAPSHOT_H5_LITE=1)
        endif()
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_SNAPSHOT_H5_DIAG=1)
        target_compile_definitions(gbarecomp_armv4t PRIVATE
            SMA2_SNAPSHOT_H5_DIAG=1)
    endif()
    if(SMA2_AOT_MODE_PARITY_DIAG)
        target_compile_definitions(SuperMarioWorldRecomp PRIVATE
            SMA2_AOT_MODE_PARITY_DIAG=1)
        target_compile_definitions(gbarecomp_runtime PRIVATE
            SMA2_AOT_MODE_PARITY_DIAG=1)
    endif()
endif()

if(SMA2_CART_AOT_DIAG_RESULT_HOST_TESTS)
    if(CMAKE_CROSSCOMPILING OR SMA2_DREAMCAST)
        message(FATAL_ERROR "SMA2_CART_AOT_DIAG_RESULT_HOST_TESTS requires a native host build")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    enable_testing()
    add_executable(sma2_cart_aot_diag_result_probe
        tools/sma2_cart_aot_diag_result_probe.cpp)
    target_include_directories(sma2_cart_aot_diag_result_probe PRIVATE src)
    add_test(NAME sma2_cart_aot_diag_result_tests
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_sma2_cart_aot_diag_result.py"
            "$<TARGET_FILE:sma2_cart_aot_diag_result_probe>")
endif()

if(SMA2_PILOT_WINDOW_HOST_TESTS)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "SMA2_PILOT_WINDOW_HOST_TESTS must use a native host compiler")
    endif()
    enable_testing()
    add_executable(sma2_pilot_window_tests tools/sma2_pilot_window_tests.cpp)
    target_include_directories(sma2_pilot_window_tests PRIVATE src/snapshots)
    add_test(NAME sma2_pilot_window_tests COMMAND sma2_pilot_window_tests)
endif()

if(SMA2_RGB565_HOST_TESTS)
    if(SMA2_DREAMCAST)
        message(FATAL_ERROR "SMA2_RGB565_HOST_TESTS must use a native host compiler")
    endif()
    enable_testing()
    add_executable(sma2_rgb565_sidecar_present_tests
        tools/test_rgb565_sidecar_present.cpp)
    target_include_directories(sma2_rgb565_sidecar_present_tests PRIVATE src)
    add_test(NAME sma2_rgb565_sidecar_present_tests
        COMMAND sma2_rgb565_sidecar_present_tests)
endif()

if(SMA2_NATIVE_PVR_BRIDGE_HOST_TESTS)
    if(SMA2_DREAMCAST)
        message(FATAL_ERROR "SMA2_NATIVE_PVR_BRIDGE_HOST_TESTS must use a native host compiler")
    endif()
    enable_testing()
    add_executable(sma2_native_pvr_bridge_contract_tests
        tools/test_native_pvr_bridge_contract.cpp)
    target_include_directories(sma2_native_pvr_bridge_contract_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GBARECOMP_ROOT}/src/gba"
        "${GBARECOMP_ROOT}/src/armv4t")
    add_test(NAME sma2_native_pvr_bridge_contract_tests
        COMMAND sma2_native_pvr_bridge_contract_tests)
endif()

if(SMA2_SNAPSHOT_TESTS)
    if(SMA2_DREAMCAST)
        message(FATAL_ERROR "SMA2_SNAPSHOT_TESTS is a native-host target")
    endif()
    if(NOT EXISTS "${SMA2_SNAPSHOT_ROM}")
        message(FATAL_ERROR "SMA2_SNAPSHOT_ROM not found: ${SMA2_SNAPSHOT_ROM}")
    endif()
    if(NOT EXISTS "${SMA2_SNAPSHOT_BIOS}")
        message(FATAL_ERROR "SMA2_SNAPSHOT_BIOS not found: ${SMA2_SNAPSHOT_BIOS}")
    endif()
    enable_testing()
    add_executable(sma2_snapshot_diff
        tools/sma2_snapshot_diff.cpp
        src/snapshots/sma2_checkpoint.cpp)
    target_include_directories(sma2_snapshot_diff PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GBARECOMP_ROOT}/src/armv4t"
        "${GBARECOMP_ROOT}/src/gba"
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/snapshots")
    # Opt-in: link a generated cart corpus so the harness can run the
    # --native-replay-from differential. Empty (default) keeps the interpreter-only
    # harness with its empty dispatch stub.
    set(SMA2_SNAPSHOT_NATIVE_CORPUS_DIR "" CACHE PATH
        "Generated cart corpus linked into sma2_snapshot_diff for --native-replay-from")
    if(SMA2_SNAPSHOT_NATIVE_CORPUS_DIR)
        if(NOT EXISTS "${SMA2_SNAPSHOT_NATIVE_CORPUS_DIR}/dispatch_table.cpp")
            message(FATAL_ERROR "SMA2_SNAPSHOT_NATIVE_CORPUS_DIR has no dispatch_table.cpp")
        endif()
        file(GLOB SMA2_SNAPSHOT_CORPUS_SOURCES CONFIGURE_DEPENDS
            "${SMA2_SNAPSHOT_NATIVE_CORPUS_DIR}/*.cpp")
        set(SMA2_SNAPSHOT_CORPUS_SHARDS ${SMA2_SNAPSHOT_CORPUS_SOURCES})
        list(FILTER SMA2_SNAPSHOT_CORPUS_SHARDS INCLUDE REGEX "/recompiled_[0-9]+\\.cpp$")
        set_source_files_properties(${SMA2_SNAPSHOT_CORPUS_SHARDS} PROPERTIES
            COMPILE_OPTIONS "-O${SMA2_CART_AOT_OPT};-g0;-Wno-unused-variable;-Wno-unused-but-set-variable")
        target_sources(sma2_snapshot_diff PRIVATE ${SMA2_SNAPSHOT_CORPUS_SOURCES})
        target_include_directories(sma2_snapshot_diff PRIVATE
            "${SMA2_SNAPSHOT_NATIVE_CORPUS_DIR}")
        target_compile_definitions(sma2_snapshot_diff PRIVATE SMA2_SNAPSHOT_NATIVE_CORPUS=1)
        # Harness-only exact native-instruction counter/stop (see the tool).
        target_link_options(sma2_snapshot_diff PRIVATE "-Wl,--wrap=runtime_should_yield")
    endif()
    target_link_libraries(sma2_snapshot_diff PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime
        gbarecomp_debug
        gbarecomp_gba
        gbarecomp_armv4t
        "-Wl,--end-group")
    add_test(NAME sma2_snapshot_diff
        COMMAND sma2_snapshot_diff
            --rom "${SMA2_SNAPSHOT_ROM}"
            --bios "${SMA2_SNAPSHOT_BIOS}")
endif()

option(GBAGAME_RECOMP_UI "Build the recomp-ui pre-boot launcher" ON)
if(GBAGAME_RECOMP_UI)
    include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
    add_library(game_launcher_ui STATIC src/game_launcher_boot.cpp)
    target_include_directories(game_launcher_ui PRIVATE
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/gba"
        src)
    target_compile_definitions(game_launcher_ui PRIVATE
        GBARECOMP_ENABLE_MODS=1)
    recomp_target_launcher_ui(game_launcher_ui CONSOLE gba)
    find_package(SDL2 REQUIRED)
    target_link_libraries(game_launcher_ui PUBLIC SDL2::SDL2)
    if(WIN32)
        target_link_libraries(game_launcher_ui PUBLIC opengl32)
    endif()
    target_link_libraries(SuperMarioWorldRecomp PRIVATE game_launcher_ui)
    set_property(SOURCE src/main.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS GBAGAME_RECOMP_UI)
endif()

# Ship the game-owned, disabled-by-default adaptive-view package beside the
# executable. User-installed .gbamod archives join this same catalog.
set(SMW_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(EXISTS "${SMW_PRELOADED_MODS}/packages")
    add_custom_command(TARGET SuperMarioWorldRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SMW_PRELOADED_MODS}"
            "$<TARGET_FILE_DIR:SuperMarioWorldRecomp>/mods"
        COMMENT "Preloading the Super Mario World mod catalog")
endif()

if(MSVC)
    target_link_options(SuperMarioWorldRecomp PRIVATE /STACK:16777216)
elseif(MINGW OR WIN32)
    target_link_options(SuperMarioWorldRecomp PRIVATE -Wl,--stack,16777216)
endif()
