#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# Usage: ./unixBuild.sh [Debug|Release] [SDK install prefix] [native|arm64|x86_64|all]
# Existing calls without an architecture keep their install prefix semantics.
case "${1:-Debug}" in
  Debug|debug) BUILD_TYPE=Debug ;;
  Release|release) BUILD_TYPE=Release ;;
  *) echo "Expected Debug or Release as the first argument." >&2; exit 1 ;;
esac
if (( $# > 3 )); then
  echo "Usage: $0 [Debug|Release] [SDK install prefix] [native|arm64|x86_64|all]" >&2
  exit 1
fi

PLATFORM=$(uname -s)
ARCH=${3:-native}
INSTALL_DIR=${2:-"${SCRIPT_DIR}/install"}
# Resolve relative prefixes consistently for configure and install.
if [[ "${INSTALL_DIR}" != /* ]]; then
  INSTALL_DIR="${SCRIPT_DIR}/${INSTALL_DIR}"
fi
if [[ "${PLATFORM}" == Darwin ]]; then
  MACOS_SDK=${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}
  [[ "${ARCH}" != native ]] || ARCH=$(uname -m)
  case "${ARCH}" in
    arm64|x86_64) ARCHITECTURES=("${ARCH}") ;;
    all) ARCHITECTURES=(arm64 x86_64) ;;
    *) echo "Expected native, arm64, x86_64, or all as the architecture." >&2; exit 1 ;;
  esac
else
  if [[ "${ARCH}" != native ]]; then
    echo "Architecture selection is currently supported only on macOS." >&2
    exit 1
  fi
  ARCHITECTURES=(native)
fi

mkdir -p build
echo "${BUILD_TYPE}" > build/.active_build
for TARGET_ARCH in "${ARCHITECTURES[@]}"; do
  CONFIGURE_ARGS=()
  TARGET_DIR="build/${BUILD_TYPE}"
  PREFIX="${INSTALL_DIR}"
  if [[ "${PLATFORM}" == Darwin ]]; then
    TARGET_DIR="build/macos-${TARGET_ARCH}/${BUILD_TYPE}"
    CONFIGURE_ARGS+=("-DCMAKE_OSX_SYSROOT=${MACOS_SDK}" "-DCMAKE_OSX_ARCHITECTURES=${TARGET_ARCH}")
    if [[ "${ARCH}" == all || ( $# -ge 3 && -z "${2:-}" ) ]]; then
      PREFIX="${INSTALL_DIR}/macos-${TARGET_ARCH}"
    fi
  fi
  echo "Building vibrance-engine ${BUILD_TYPE} (${TARGET_ARCH})"
  echo "Installing vibrance-engine SDK to: ${PREFIX}"
  cmake -S "${SCRIPT_DIR}" -B "${TARGET_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    ${CONFIGURE_ARGS[@]+"${CONFIGURE_ARGS[@]}"}
  cmake --build "${TARGET_DIR}" --config "${BUILD_TYPE}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
  cmake --install "${TARGET_DIR}" --prefix "${PREFIX}" --config "${BUILD_TYPE}"
done
