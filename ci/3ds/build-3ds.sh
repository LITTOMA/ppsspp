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
BANNERTOOL_REPO="${BANNERTOOL_REPO:-https://github.com/Epicpkmn11/bannertool.git}"
CTR3DSTOOL_REPO="${CTR3DSTOOL_REPO:-https://github.com/dnasdw/3dstool.git}"
CTR3DSTOOL_VERSION="${CTR3DSTOOL_VERSION:-v1.2.6}"
LOCAL_TOOL_DIR="${LOCAL_TOOL_DIR:-${ROOT_DIR}/.ci-tools/bin}"
ZIM_UNPACK_TOOL="${LOCAL_TOOL_DIR}/zim-unpack"

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
  mkdir -p "${LOCAL_TOOL_DIR}"
  install -m 0755 "${tmp_dir}/Project_CTR/makerom/bin/makerom" "${LOCAL_TOOL_DIR}/makerom"
  rm -rf "${tmp_dir}"
}

ensure_bannertool() {
  if command -v bannertool >/dev/null 2>&1; then
    return 0
  fi

  require_tool git
  require_tool make
  require_tool install

  local os arch tmp_dir
  os="$(uname -s)"
  arch="$(uname -m)"
  if [[ "${os}" != "Linux" || "${arch}" != "x86_64" ]]; then
    printf '[3ds-ci] bannertool is missing and automatic install only supports Linux x86_64, got %s %s\n' "${os}" "${arch}" >&2
    return 1
  fi

  tmp_dir="$(mktemp -d)"
  log "Building bannertool"
  git config --global url.https://github.com/.insteadOf git://github.com/ >/dev/null 2>&1 || true
  git clone --depth 1 --recursive "${BANNERTOOL_REPO}" "${tmp_dir}/bannertool"
  make -C "${tmp_dir}/bannertool" -j"${JOBS}"
  mkdir -p "${LOCAL_TOOL_DIR}"
  install -m 0755 "${tmp_dir}/bannertool/output/linux-x86_64/bannertool" "${LOCAL_TOOL_DIR}/bannertool"
  rm -rf "${tmp_dir}"
}

ensure_3dstool() {
  if command -v 3dstool >/dev/null 2>&1; then
    mkdir -p "${LOCAL_TOOL_DIR}"
    : > "${LOCAL_TOOL_DIR}/ignore_3dstool.txt"
    return 0
  fi

  require_tool git
  require_tool cmake
  require_tool make
  require_tool install

  local os arch tmp_dir tool_path
  os="$(uname -s)"
  arch="$(uname -m)"
  if [[ "${os}" != "Linux" || "${arch}" != "x86_64" ]]; then
    printf '[3ds-ci] 3dstool is missing and automatic install only supports Linux x86_64, got %s %s\n' "${os}" "${arch}" >&2
    return 1
  fi

  tmp_dir="$(mktemp -d)"
  log "Building 3dstool ${CTR3DSTOOL_VERSION}"
  if ! git clone --depth 1 --branch "${CTR3DSTOOL_VERSION}" "${CTR3DSTOOL_REPO}" "${tmp_dir}/3dstool"; then
    rm -rf "${tmp_dir}/3dstool"
    git clone --depth 1 "${CTR3DSTOOL_REPO}" "${tmp_dir}/3dstool"
  fi
  cmake -S "${tmp_dir}/3dstool" -B "${tmp_dir}/3dstool-build" \
    -DUSE_DEP=ON \
    -DBUILD64=ON \
    -DCMAKE_EXE_LINKER_FLAGS=-no-pie
  cmake --build "${tmp_dir}/3dstool-build" --parallel "${JOBS}"

  tool_path="${tmp_dir}/3dstool/bin/Release/3dstool"
  if [[ ! -x "${tool_path}" ]]; then
    tool_path="${tmp_dir}/3dstool-build/bin/Release/3dstool"
  fi
  if [[ ! -x "${tool_path}" ]]; then
    tool_path="$(find "${tmp_dir}/3dstool" "${tmp_dir}/3dstool-build" -type f -name 3dstool -perm -111 | head -n 1 || true)"
  fi
  if [[ -z "${tool_path}" || ! -x "${tool_path}" ]]; then
    printf '[3ds-ci] failed to locate built 3dstool executable\n' >&2
    return 1
  fi

  mkdir -p "${LOCAL_TOOL_DIR}"
  install -m 0755 "${tool_path}" "${LOCAL_TOOL_DIR}/3dstool"
  : > "${LOCAL_TOOL_DIR}/ignore_3dstool.txt"
  rm -rf "${tmp_dir}"
}

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
export PATH="${LOCAL_TOOL_DIR}:${DEVKITARM}/bin:${DEVKITPRO}/tools/bin:${PATH}"

if [[ "${CI_DKP_INSTALL}" == "1" ]] && command -v dkp-pacman >/dev/null 2>&1; then
  log "Ensuring devkitPro 3DS packages are installed"
  dkp-pacman -Sy --noconfirm --needed 3ds-dev
fi

require_tool cmake
require_tool cc
require_tool arm-none-eabi-gcc
require_tool 3dsxtool
require_tool python3
ensure_makerom
require_tool makerom
ensure_bannertool
require_tool bannertool
ensure_3dstool
require_tool 3dstool

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

log "Preparing 3DS RomFS assets"
mkdir -p "${LOCAL_TOOL_DIR}"
cc -O2 -DNDEBUG -DZSTD_DISABLE_ASM \
  -I"${ROOT_DIR}/ext/zstd/lib" \
  -I"${ROOT_DIR}/ext/zstd/lib/common" \
  -I"${ROOT_DIR}/ext/zstd/lib/decompress" \
  "${ROOT_DIR}/ci/3ds/zim-unpack.c" \
  "${ROOT_DIR}"/ext/zstd/lib/common/*.c \
  "${ROOT_DIR}"/ext/zstd/lib/decompress/*.c \
  -o "${ZIM_UNPACK_TOOL}"

for zim in \
  "${BUILD_DIR}/assets/asciifont_atlas.zim"; do
  if [[ -f "${zim}" ]]; then
    tmp_zim="${zim}.3ds-unpacked"
    "${ZIM_UNPACK_TOOL}" "${zim}" "${tmp_zim}"
    mv -f "${tmp_zim}" "${zim}"
  fi
done
rm -f \
  "${BUILD_DIR}/3DS/PPSSPP.3dsx" \
  "${BUILD_DIR}/3DS/PPSSPP.cia" \
  "${BUILD_DIR}/3DS/PPSSPP.romfs"

log "Building with ${JOBS} parallel jobs"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}" 2>&1 | tee "${BUILD_LOG}"

log "Collecting build outputs"
find "${BUILD_DIR}" -type f \( -name '*.3dsx' -o -name '*.cia' -o -name '*.elf' -o -name '*.smdh' -o -name '*.bnr' -o -name '*.banner.png' -o -name '*.romfs' \) -print0 |
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
