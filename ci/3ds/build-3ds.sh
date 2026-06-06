#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-3ds}"
ARTIFACT_DIR="${ARTIFACT_DIR:-${ROOT_DIR}/artifacts/3ds}"
TOOLCHAIN_FILE="${TOOLCHAIN_FILE:-${ROOT_DIR}/cmake/Toolchain-devkitARM-3DS.cmake}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GENERATOR="${GENERATOR:-Unix Makefiles}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
REQUIRE_3DS_ARTIFACTS="${REQUIRE_3DS_ARTIFACTS:-1}"
CI_DKP_INSTALL="${CI_DKP_INSTALL:-0}"
MAKEROM_VERSION="${MAKEROM_VERSION:-makerom-v0.19.0}"

log() {
  printf '[3ds-ci] %s\n' "$*"
}

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf '[3ds-ci] missing required tool: %s\n' "$1" >&2
    return 1
  fi
}

ensure_makerom() {
  if command -v makerom >/dev/null 2>&1; then
    return 0
  fi

  require_tool git
  require_tool make
  require_tool install

  local os arch tmp_dir
  os="$(uname -s)"
  arch="$(uname -m)"
  if [[ "${os}" != "Linux" || "${arch}" != "x86_64" ]]; then
    printf '[3ds-ci] makerom is missing and automatic install only supports Linux x86_64, got %s %s\n' "${os}" "${arch}" >&2
    return 1
  fi

  tmp_dir="$(mktemp -d)"
  log "Building makerom ${MAKEROM_VERSION} from Project_CTR"
  git clone --depth 1 --branch "${MAKEROM_VERSION}" https://github.com/3DSGuy/Project_CTR.git "${tmp_dir}/Project_CTR"
  make -C "${tmp_dir}/Project_CTR/makerom" deps
  make -C "${tmp_dir}/Project_CTR/makerom" -j"${JOBS}"
  install -m 0755 "${tmp_dir}/Project_CTR/makerom/bin/makerom" "${DEVKITPRO}/tools/bin/makerom"
  rm -rf "${tmp_dir}"
}

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
export PATH="${DEVKITARM}/bin:${DEVKITPRO}/tools/bin:${PATH}"

if [[ "${CI_DKP_INSTALL}" == "1" ]] && command -v dkp-pacman >/dev/null 2>&1; then
  log "Ensuring devkitPro 3DS packages are installed"
  dkp-pacman -Sy --noconfirm --needed 3ds-dev
fi

require_tool cmake
require_tool arm-none-eabi-gcc
require_tool 3dsxtool
require_tool smdhtool
require_tool mkromfs3ds
require_tool python3
ensure_makerom
require_tool makerom

if [[ ! -f "${ROOT_DIR}/CMakeLists.txt" ]]; then
  printf '[3ds-ci] %s does not contain CMakeLists.txt. Run this in a PPSSPP source checkout.\n' "${ROOT_DIR}" >&2
  exit 64
fi

if command -v git >/dev/null 2>&1; then
  git config --global --add safe.directory "${ROOT_DIR}" >/dev/null 2>&1 || true
fi

mkdir -p "${BUILD_DIR}" "${ARTIFACT_DIR}"
CONFIGURE_LOG="${ARTIFACT_DIR}/cmake-configure.log"
BUILD_LOG="${ARTIFACT_DIR}/cmake-build.log"

CMAKE_FLAGS=(
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
  "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
  "-DPPSSPP_PLATFORM=3DS"
  "-DPPSSPP_TARGET=3DS"
  "-DUSING_GLES2=OFF"
  "-DUSE_VULKAN=OFF"
  "-DUSING_X11_VULKAN=OFF"
  "-DUSE_DISCORD=OFF"
  "-DUSE_SYSTEM_FFMPEG=OFF"
  "-DUSING_QT_UI=OFF"
  "-DUNITTEST=OFF"
)

if [[ -n "${PPSSPP_3DS_CMAKE_FLAGS:-}" ]]; then
  # shellcheck disable=SC2206
  EXTRA_FLAGS=( ${PPSSPP_3DS_CMAKE_FLAGS} )
  CMAKE_FLAGS+=( "${EXTRA_FLAGS[@]}" )
fi

log "Configuring PPSSPP for Nintendo 3DS"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "${GENERATOR}" "${CMAKE_FLAGS[@]}" "$@" 2>&1 | tee "${CONFIGURE_LOG}"

log "Building with ${JOBS} parallel jobs"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}" 2>&1 | tee "${BUILD_LOG}"

log "Collecting build outputs"
find "${BUILD_DIR}" -type f \( -name '*.3dsx' -o -name '*.cia' -o -name '*.elf' -o -name '*.smdh' -o -name '*.romfs' \) -print0 |
  while IFS= read -r -d '' file; do
    cp -f "${file}" "${ARTIFACT_DIR}/$(basename "${file}")"
  done

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cp -f "${BUILD_DIR}/CMakeCache.txt" "${ARTIFACT_DIR}/CMakeCache.txt"
fi

if [[ -d "${BUILD_DIR}/assets" ]]; then
  rm -rf "${ARTIFACT_DIR}/assets"
  cp -a "${BUILD_DIR}/assets" "${ARTIFACT_DIR}/assets"
fi

if ! find "${ARTIFACT_DIR}" -maxdepth 1 -type f \( -name '*.3dsx' -o -name '*.cia' \) | grep -q .; then
  first_elf="$(find "${BUILD_DIR}" -type f \( -iname 'PPSSPP*.elf' -o -iname 'ppsspp*.elf' \) | head -n 1 || true)"
  if [[ -n "${first_elf}" && -x "$(command -v 3dsxtool || true)" ]]; then
    log "Converting ${first_elf} to PPSSPP.3dsx"
    3dsxtool "${first_elf}" "${ARTIFACT_DIR}/PPSSPP.3dsx"
  fi
fi

if [[ "${REQUIRE_3DS_ARTIFACTS}" == "1" ]]; then
  if ! find "${ARTIFACT_DIR}" -maxdepth 1 -type f \( -name '*.3dsx' -o -name '*.cia' \) | grep -q .; then
    printf '[3ds-ci] build completed but did not produce .3dsx or .cia artifacts.\n' >&2
    printf '[3ds-ci] This usually means the 3DS PPSSPP platform target still needs to be wired into CMake.\n' >&2
    exit 65
  fi
fi

log "Artifacts:"
find "${ARTIFACT_DIR}" -maxdepth 1 -type f -print | sort
