#!/usr/bin/env bash
set -eo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gbarecomp_root="${GBARECOMP_ROOT:-/home/gpf/code/gbarecomp}"
build_dir="${SMA2_DC_BUILD_DIR:-${repo_root}/build-dreamcast}"
tomlpp_include="${GBARECOMP_TOMLPP_INCLUDE_DIR:-/usr/include/toml++}"
generated_dir="${SMA2_GENERATED_DIR:-${repo_root}/generated}"
require_generated_dispatch="${SMA2_REQUIRE_GENERATED_DISPATCH:-${SMA2_CART_AOT:-OFF}}"

if [[ ! -f "${gbarecomp_root}/CMakeLists.txt" ]]; then
    echo "GBARecomp checkout not found: ${gbarecomp_root}" >&2
    echo "Set GBARECOMP_ROOT to an existing local checkout; this script does not initialize submodules." >&2
    exit 2
fi
if [[ ! -f "${tomlpp_include}/toml.hpp" ]]; then
    echo "toml++ single-header include not found: ${tomlpp_include}/toml.hpp" >&2
    echo "Set GBARECOMP_TOMLPP_INCLUDE_DIR to a local toml++ include directory." >&2
    exit 2
fi
if [[ "${SMA2_RGB565_SIDECAR:-OFF}" != "ON" && "${SMA2_RGB565_SIDECAR:-OFF}" != "OFF" ]]; then
    echo "SMA2_RGB565_SIDECAR must be ON or OFF" >&2
    exit 2
fi
for option_name in SMA2_NATIVE_PVR_GBA_RENDER SMA2_NATIVE_PVR_SKIP_SOFTWARE SMA2_NATIVE_PVR_FORCE_FALLBACK SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG SMA2_NATIVE_PVR_INFLIGHT_DIAG SMA2_NATIVE_PVR_COST_DIAG SMA2_PPU_COMPOSITOR_COST_DIAG SMA2_PPU_SCANLINE_COST_DIAG; do
    option_value="${!option_name:-OFF}"
    if [[ "${option_value}" != "ON" && "${option_value}" != "OFF" ]]; then
        echo "${option_name} must be ON or OFF" >&2
        exit 2
    fi
done
if [[ "${SMA2_NATIVE_PVR_FRONT_CAPTURE:-OFF}" != "ON" && "${SMA2_NATIVE_PVR_FRONT_CAPTURE:-OFF}" != "OFF" ]]; then
    echo "SMA2_NATIVE_PVR_FRONT_CAPTURE must be ON or OFF" >&2
    exit 2
fi
if [[ "${SMA2_NATIVE_PVR_FRONT_CAPTURE:-OFF}" == "ON" && "${SMA2_NATIVE_PVR_GBA_RENDER:-OFF}" != "ON" ]]; then
    echo "SMA2_NATIVE_PVR_FRONT_CAPTURE=ON requires SMA2_NATIVE_PVR_GBA_RENDER=ON" >&2
    exit 2
fi
if [[ ! "${SMA2_NATIVE_PVR_DIAG_STOP_FRAMES:-0}" =~ ^(0|[1-9][0-9]*)$ ]]; then
    echo "SMA2_NATIVE_PVR_DIAG_STOP_FRAMES must be 0 or a positive integer" >&2
    exit 2
fi
if [[ "${SMA2_NATIVE_PVR_DIAG_STOP_FRAMES:-0}" != "0" && ( "${SMA2_NATIVE_PVR_INFLIGHT_DIAG:-OFF}" != "ON" || "${SMA2_NATIVE_PVR_FRONT_CAPTURE:-OFF}" != "OFF" ) ]]; then
    echo "SMA2_NATIVE_PVR_DIAG_STOP_FRAMES requires in-flight diagnostics ON and front capture OFF" >&2
    exit 2
fi
if [[ ! "${SMA2_NATIVE_PVR_CAPTURE_FRAMES:-1}" =~ ^[1-9][0-9]*$ ]]; then
    echo "SMA2_NATIVE_PVR_CAPTURE_FRAMES must be a positive integer" >&2
    exit 2
fi
if [[ ! "${SMA2_NATIVE_PVR_CAPTURE_START_FRAME:-201}" =~ ^[1-9][0-9]*$ ]]; then
    echo "SMA2_NATIVE_PVR_CAPTURE_START_FRAME must be a positive integer" >&2
    exit 2
fi
if [[ "${SMA2_NATIVE_PVR_SKIP_SOFTWARE:-OFF}" == "ON" && "${SMA2_NATIVE_PVR_GBA_RENDER:-OFF}" != "ON" ]]; then
    echo "SMA2_NATIVE_PVR_SKIP_SOFTWARE=ON requires SMA2_NATIVE_PVR_GBA_RENDER=ON" >&2
    exit 2
fi
if [[ "${SMA2_NATIVE_PVR_FORCE_FALLBACK:-OFF}" == "ON" && "${SMA2_NATIVE_PVR_GBA_RENDER:-OFF}" != "ON" ]]; then
    echo "SMA2_NATIVE_PVR_FORCE_FALLBACK=ON requires SMA2_NATIVE_PVR_GBA_RENDER=ON" >&2
    exit 2
fi
if [[ "${SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE:-OFF}" == "ON" && ( "${SMA2_NATIVE_PVR_FRONT_CAPTURE:-OFF}" != "ON" || "${SMA2_NATIVE_PVR_SKIP_SOFTWARE:-OFF}" != "OFF" || "${SMA2_NATIVE_PVR_FORCE_FALLBACK:-OFF}" != "OFF" ) ]]; then
    echo "SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE=ON requires front capture, software composition ON, and global force fallback OFF" >&2
    exit 2
fi
if [[ "${SMA2_CART_AOT_DIAG:-OFF}" == "ON" && "${SMA2_CART_AOT:-OFF}" != "ON" ]]; then
    echo "SMA2_CART_AOT_DIAG=ON requires SMA2_CART_AOT=ON" >&2
    exit 2
fi
if [[ "${SMA2_CART_AOT_IRQ_DIAG:-OFF}" == "ON" && "${SMA2_CART_AOT_DIAG:-OFF}" != "ON" ]]; then
    echo "SMA2_CART_AOT_IRQ_DIAG=ON requires SMA2_CART_AOT_DIAG=ON" >&2
    exit 2
fi
if [[ "${SMA2_DEV_CHECKPOINT_AUTOLOAD:-OFF}" == "ON" && "${SMA2_DEV_CHECKPOINT:-OFF}" != "ON" ]]; then
    echo "SMA2_DEV_CHECKPOINT_AUTOLOAD=ON requires SMA2_DEV_CHECKPOINT=ON" >&2
    exit 2
fi
if [[ "${SMA2_MEMORY_ALLOC_DIAG:-OFF}" != "ON" && "${SMA2_MEMORY_ALLOC_DIAG:-OFF}" != "OFF" ]]; then
    echo "SMA2_MEMORY_ALLOC_DIAG must be ON or OFF" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" != "ON" && "${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" != "OFF" ]]; then
    echo "SMA2_SNAPSHOT_INPUT_DIAG must be ON or OFF" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" == "ON" && "${SMA2_DEV_CHECKPOINT:-OFF}" != "ON" ]]; then
    echo "SMA2_SNAPSHOT_INPUT_DIAG=ON requires SMA2_DEV_CHECKPOINT=ON" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" == "ON" && "${SMA2_DEV_CHECKPOINT_AUTOLOAD:-OFF}" == "ON" ]]; then
    echo "SMA2_SNAPSHOT_INPUT_DIAG requires SMA2_DEV_CHECKPOINT_AUTOLOAD=OFF" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" == "ON" && "${SMA2_MEMORY_ALLOC_DIAG:-OFF}" == "ON" ]]; then
    echo "SMA2_SNAPSHOT_INPUT_DIAG cannot be combined with SMA2_MEMORY_ALLOC_DIAG" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_H5_DIAG:-OFF}" != "ON" && "${SMA2_SNAPSHOT_H5_DIAG:-OFF}" != "OFF" ]]; then
    echo "SMA2_SNAPSHOT_H5_DIAG must be ON or OFF" >&2
    exit 2
fi
if [[ "${SMA2_AOT_MODE_PARITY_DIAG:-OFF}" != "ON" && "${SMA2_AOT_MODE_PARITY_DIAG:-OFF}" != "OFF" ]]; then
    echo "SMA2_AOT_MODE_PARITY_DIAG must be ON or OFF" >&2
    exit 2
fi
if [[ "${SMA2_AOT_MODE_PARITY_DIAG:-OFF}" == "ON" && "${SMA2_SNAPSHOT_H5_DIAG:-OFF}" != "ON" ]]; then
    echo "SMA2_AOT_MODE_PARITY_DIAG=ON requires SMA2_SNAPSHOT_H5_DIAG=ON" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_H5_DIAG:-OFF}" == "ON" && "${SMA2_DEV_CHECKPOINT:-OFF}" != "ON" ]]; then
    echo "SMA2_SNAPSHOT_H5_DIAG=ON requires SMA2_DEV_CHECKPOINT=ON" >&2
    exit 2
fi
if [[ "${SMA2_SNAPSHOT_H5_DIAG:-OFF}" == "ON" && ( "${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" == "ON" || "${SMA2_MEMORY_ALLOC_DIAG:-OFF}" == "ON" ) ]]; then
    echo "SMA2_SNAPSHOT_H5_DIAG cannot be combined with another snapshot/input diagnostic" >&2
    exit 2
fi

overlay="${build_dir}/gbarecomp-overlay"
mkdir -p "${build_dir}"

if [[ "${SMA2_CART_AOT:-OFF}" == "ON" ]]; then
    rom_path="${SMA2_CART_AOT_ROM:-${repo_root}/roms/Super Mario Advance 2 - Super Mario World (USA).gba}"
    config_path="${SMA2_CART_AOT_CONFIG:-${repo_root}/tools/sma2_minimal_dispatch.toml}"
    host_build_dir="${SMA2_CART_AOT_HOST_BUILD_DIR:-${build_dir}-cart-host}"
    generated_dir="${SMA2_GENERATED_DIR:-${build_dir}/cart-generated}"

    cmake -S "${repo_root}" -B "${host_build_dir}" \
        -DGBARECOMP_ROOT="${gbarecomp_root}" \
        -DGBARECOMP_TOMLPP_INCLUDE_DIR="${tomlpp_include}" \
        -DGBAGAME_RECOMP_UI=OFF \
        -DGBARECOMP_ENABLE_MODS=OFF \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "${host_build_dir}" --target gba_recompile \
        --parallel "${JOBS:-4}"
    "${host_build_dir}/gbarecomp_build/gba_recompile" \
        --rom "${rom_path}" \
        --config "${config_path}" \
        --out "${generated_dir}" \
        --max-functions "${SMA2_CART_AOT_MAX_FUNCTIONS:-4096}"
fi

if [[ "${SMA2_SH4_AOT_POC:-OFF}" == "ON" ]]; then
    rom_path="${SMA2_AOT_ROM:-${repo_root}/roms/Super Mario Advance 2 - Super Mario World (USA).gba}"
    host_build_dir="${SMA2_AOT_HOST_BUILD_DIR:-${build_dir}-aot-host}"
    cmake -S "${repo_root}" -B "${host_build_dir}" \
        -DGBARECOMP_ROOT="${gbarecomp_root}" \
        -DGBARECOMP_TOMLPP_INCLUDE_DIR="${tomlpp_include}" \
        -DGBAGAME_RECOMP_UI=OFF \
        -DGBARECOMP_ENABLE_MODS=OFF \
        -DSMA2_BUILD_AOT_CODEGEN=ON \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "${host_build_dir}" --target sma2_aot_codegen \
        --parallel "${JOBS:-4}"
    python3 "${repo_root}/tools/generate_sma2_aot.py" \
        --generator "${host_build_dir}/sma2_aot_codegen" \
        --rom "${rom_path}" \
        --output "${build_dir}/sma2_aot_stage_b.generated.cpp"
fi

source /opt/toolchains/dc/kos/environ.sh
# Bind KOS configure and build to the compiler that actually resolves after
# sourcing environ.sh. Its install prefix has moved on this machine over time.
sh_gcc="$(command -v sh-elf-gcc)"
if [[ -z "${sh_gcc}" || ! -x "${sh_gcc}" ]]; then
    echo "sh-elf-gcc is unavailable after sourcing KOS environ.sh" >&2
    exit 2
fi
sh_gcc_version="$("${sh_gcc}" -dumpversion)"
if [[ "${sh_gcc_version}" != 17.* ]]; then
    echo "Expected GCC 17 for Dreamcast; selected ${sh_gcc} (${sh_gcc_version})" >&2
    exit 2
fi
export KOS_CC_BASE="${sh_gcc%/bin/sh-elf-gcc}"
export KOS_CC="${KOS_CC_BASE}/bin/sh-elf-gcc"
export KOS_CCPLUS="${KOS_CC_BASE}/bin/sh-elf-g++"
export KOS_AR="${KOS_CC_BASE}/bin/sh-elf-ar"
export KOS_RANLIB="${KOS_CC_BASE}/bin/sh-elf-ranlib"
export PATH="${KOS_CC_BASE}/bin:${PATH}"
hash -r
if [[ "$(command -v sh-elf-gcc)" != "${KOS_CC_BASE}/bin/sh-elf-gcc" ]]; then
    echo "sh-elf-gcc did not resolve to selected GCC 17 prefix ${KOS_CC_BASE}" >&2
    exit 2
fi
python3 "${repo_root}/tools/prepare_dreamcast_framework.py" \
    "${gbarecomp_root}" "${overlay}"

kos-cmake -S "${repo_root}" -B "${build_dir}" \
    -DGBARECOMP_ROOT="${overlay}" \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DSMA2_RGB565_SOURCE_ROOT="${gbarecomp_root}" \
    -DGBARECOMP_TOMLPP_INCLUDE_DIR="${tomlpp_include}" \
    -DGBARECOMP_RUNTIME_UI_ROOT= \
    -DGBARECOMP_ENABLE_MODS=OFF \
    -DGBARECOMP_SELFHEAL_RECOMPILE_DEFAULT=OFF \
    -DGBAGAME_RECOMP_UI=OFF \
    -DSMA2_GENERATED_DIR="${generated_dir}" \
    -DSMA2_REQUIRE_GENERATED_DISPATCH="${require_generated_dispatch}" \
    -DSMA2_DREAMCAST=ON \
    -DSMA2_PVR_PRESENT="${SMA2_PVR_PRESENT:-OFF}" \
    -DSMA2_NATIVE_PVR_GBA_RENDER="${SMA2_NATIVE_PVR_GBA_RENDER:-OFF}" \
    -DSMA2_NATIVE_PVR_SKIP_SOFTWARE="${SMA2_NATIVE_PVR_SKIP_SOFTWARE:-OFF}" \
    -DSMA2_NATIVE_PVR_FRONT_CAPTURE="${SMA2_NATIVE_PVR_FRONT_CAPTURE:-OFF}" \
    -DSMA2_NATIVE_PVR_CAPTURE_FRAMES="${SMA2_NATIVE_PVR_CAPTURE_FRAMES:-1}" \
    -DSMA2_NATIVE_PVR_CAPTURE_START_FRAME="${SMA2_NATIVE_PVR_CAPTURE_START_FRAME:-201}" \
    -DSMA2_NATIVE_PVR_FORCE_FALLBACK="${SMA2_NATIVE_PVR_FORCE_FALLBACK:-OFF}" \
    -DSMA2_NATIVE_PVR_MIXED_ROW_FIXTURE="${SMA2_NATIVE_PVR_MIXED_ROW_FIXTURE:-OFF}" \
    -DSMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG="${SMA2_NATIVE_PVR_RESOURCE_WRITE_DIAG:-OFF}" \
    -DSMA2_NATIVE_PVR_COST_DIAG="${SMA2_NATIVE_PVR_COST_DIAG:-OFF}" \
    -DSMA2_PPU_COMPOSITOR_COST_DIAG="${SMA2_PPU_COMPOSITOR_COST_DIAG:-OFF}" \
    -DSMA2_PPU_SCANLINE_COST_DIAG="${SMA2_PPU_SCANLINE_COST_DIAG:-OFF}" \
    -DSMA2_NATIVE_PVR_INFLIGHT_DIAG="${SMA2_NATIVE_PVR_INFLIGHT_DIAG:-OFF}" \
    -DSMA2_NATIVE_PVR_DIAG_STOP_FRAMES="${SMA2_NATIVE_PVR_DIAG_STOP_FRAMES:-0}" \
    -DSMA2_MGBADC_ROOT="${SMA2_MGBADC_ROOT:-/tmp/sma2-bg0-mgbadc-source}" \
    -DSMA2_RGB565_SIDECAR="${SMA2_RGB565_SIDECAR:-OFF}" \
    -DSMA2_DEV_CHECKPOINT="${SMA2_DEV_CHECKPOINT:-OFF}" \
    -DSMA2_DEV_CHECKPOINT_AUTOLOAD="${SMA2_DEV_CHECKPOINT_AUTOLOAD:-OFF}" \
    -DSMA2_MEMORY_ALLOC_DIAG="${SMA2_MEMORY_ALLOC_DIAG:-OFF}" \
    -DSMA2_SNAPSHOT_INPUT_DIAG="${SMA2_SNAPSHOT_INPUT_DIAG:-OFF}" \
    -DSMA2_SNAPSHOT_H5_DIAG="${SMA2_SNAPSHOT_H5_DIAG:-OFF}" \
    -DSMA2_AOT_MODE_PARITY_DIAG="${SMA2_AOT_MODE_PARITY_DIAG:-OFF}" \
    -DSMA2_SNAPSHOT_H5_INSTRUCTIONS="${SMA2_SNAPSHOT_H5_INSTRUCTIONS:-64}" \
    -DSMA2_SNAPSHOT_H5_MIXER_EPISODES="${SMA2_SNAPSHOT_H5_MIXER_EPISODES:-0}" \
    -DSMA2_SNAPSHOT_H5_LITE="${SMA2_SNAPSHOT_H5_LITE:-OFF}" \
    -DSMA2_SH4_DYNAREC="${SMA2_SH4_DYNAREC:-OFF}" \
    -DSMA2_SH4_AOT_POC="${SMA2_SH4_AOT_POC:-OFF}" \
    -DSMA2_IWRAM_AOT="${SMA2_IWRAM_AOT:-OFF}" \
    -DSMA2_IWRAM_AOT_ISLAND="${SMA2_IWRAM_AOT_ISLAND:-OFF}" \
    -DSMA2_IWRAM_AOT_ISLAND_SWITCH="${SMA2_IWRAM_AOT_ISLAND_SWITCH:-OFF}" \
    -DSMA2_NORMAL_BENCH_FRAMES="${SMA2_NORMAL_BENCH_FRAMES:-0}" \
    -DSMA2_NORMAL_BENCH_PMU="${SMA2_NORMAL_BENCH_PMU:-OFF}" \
    -DGBARECOMP_IWRAM_CODE_WATCH="${GBARECOMP_IWRAM_CODE_WATCH:-ON}" \
    -DSMA2_AOT_TAIL_PLACEMENT="${SMA2_AOT_TAIL_PLACEMENT:-OFF}" \
    -DSMA2_LOW_PMU="${SMA2_LOW_PMU:-OFF}" \
    -DSMA2_B16_GAP="${SMA2_B16_GAP:-OFF}" \
    -DSMA2_CART_AOT_DIAG="${SMA2_CART_AOT_DIAG:-OFF}" \
    -DSMA2_CART_AOT_IRQ_DIAG="${SMA2_CART_AOT_IRQ_DIAG:-OFF}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-include ${repo_root}/tools/kos/glibcxx_c99_shim.h ${SMA2_EXTRA_CXX_FLAGS:-}"

build_log="${build_dir}/build.log"
if ! cmake --build "${build_dir}" --target SuperMarioWorldRecomp \
    --parallel "${JOBS:-4}" >"${build_log}" 2>&1; then
    tail -n 100 "${build_log}" >&2
    exit 1
fi

echo "Dreamcast ELF: ${build_dir}/SuperMarioWorldRecomp.elf"
echo "Link map:      ${build_dir}/SuperMarioWorldRecomp.map"
