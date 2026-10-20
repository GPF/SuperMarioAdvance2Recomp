#!/usr/bin/env bash
# FALLBACK: create a clean checkout of the pinned GBARecomp commit. Reads only
# committed objects from SOURCE, so a dirty working tree there is never copied.
#   tools/setup_gbarecomp_pinned.sh [DEST] [SOURCE]
# SOURCE defaults to the preserved bundle; a repo path or URL also works.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${repo_root}/tools/gbarecomp_pin.env"
dest="${1:-${GBARECOMP_FALLBACK_ROOT}}"
src="${2:-${GBARECOMP_PIN_BUNDLE}}"
core_src="${ARM_RECOMP_CORE_SOURCE:-${ARM_RECOMP_CORE_PIN_BUNDLE}}"
if [[ -e "${dest}" ]]; then
    echo "refusing to touch existing ${dest}; remove it or pick another DEST" >&2
    exit 2
fi
git clone -q --no-hardlinks "${src}" "${dest}"
git -C "${dest}" checkout -q --detach "${GBARECOMP_PIN_COMMIT}"
# arm-recomp-core is a submodule gitlink; fetch it explicitly at its pinned revision.
git clone -q --no-hardlinks "${core_src}" "${dest}/external/arm-recomp-core"
git -C "${dest}/external/arm-recomp-core" checkout -q --detach "${ARM_RECOMP_CORE_PIN_COMMIT}"
echo "GBARecomp ${GBARECOMP_PIN_COMMIT} + arm-recomp-core ${ARM_RECOMP_CORE_PIN_COMMIT} at ${dest}"
