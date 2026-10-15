#!/usr/bin/env bash
set -eo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gbarecomp_root="${GBARECOMP_ROOT:-/home/gpf/code/gbarecomp}"
build_dir="${SMA2_DC_BUILD_DIR:-${repo_root}/build-dreamcast}"
tomlpp_include="${GBARECOMP_TOMLPP_INCLUDE_DIR:-/usr/include/toml++}"

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

source /opt/toolchains/dc/kos/environ.sh
overlay="${build_dir}/gbarecomp-overlay"
mkdir -p "${build_dir}"
python3 "${repo_root}/tools/prepare_dreamcast_framework.py" \
    "${gbarecomp_root}" "${overlay}"

kos-cmake -S "${repo_root}" -B "${build_dir}" \
    -DGBARECOMP_ROOT="${overlay}" \
    -DGBARECOMP_TOMLPP_INCLUDE_DIR="${tomlpp_include}" \
    -DGBARECOMP_RUNTIME_UI_ROOT= \
    -DGBARECOMP_ENABLE_MODS=OFF \
    -DGBARECOMP_SELFHEAL_RECOMPILE_DEFAULT=OFF \
    -DGBAGAME_RECOMP_UI=OFF \
    -DSMA2_DREAMCAST=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-include ${repo_root}/tools/kos/glibcxx_c99_shim.h"

build_log="${build_dir}/build.log"
if ! cmake --build "${build_dir}" --target SuperMarioWorldRecomp \
    --parallel "${JOBS:-4}" >"${build_log}" 2>&1; then
    tail -n 100 "${build_log}" >&2
    exit 1
fi

echo "Dreamcast ELF: ${build_dir}/SuperMarioWorldRecomp.elf"
echo "Link map:      ${build_dir}/SuperMarioWorldRecomp.map"
