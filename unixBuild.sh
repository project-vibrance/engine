#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# Usage: ./unixBuild.sh [Debug|Release] [SDK install prefix|platform] [platform]
# Supported direct platform values: native, silicon, intel, all.
case "${1:-Debug}" in
  Debug|debug) BUILD_TYPE=Debug ;;
  Release|release) BUILD_TYPE=Release ;;
  *) echo "Expected Debug or Release as the first argument." >&2; exit 1 ;;
esac
if (( $# > 3 )); then
  echo "Usage: $0 [Debug|Release] [SDK install prefix|platform] [platform]" >&2
  exit 1
fi

PLATFORM=$(uname -s)
ARCH=native
INSTALL_DIR="${SCRIPT_DIR}/install"

if [[ $# -ge 2 ]] && [[ "${2}" =~ ^(native|silicon|intel|all)$ ]]; then
  ARCH="${2}"
elif [[ $# -ge 2 ]]; then
  INSTALL_DIR="${2}"
  if [[ $# -ge 3 ]]; then
    ARCH="${3}"
  fi
fi

if [[ "${INSTALL_DIR}" != /* ]]; then
  INSTALL_DIR="${SCRIPT_DIR}/${INSTALL_DIR}"
fi
if [[ "${PLATFORM}" == Darwin ]]; then
  MACOS_SDK=${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}
  if [[ "${ARCH}" == native ]]; then
    case "$(uname -m)" in
      arm64) ARCH=silicon ;;
      x86_64) ARCH=intel ;;
      *) echo "Unsupported macOS host architecture." >&2; exit 1 ;;
    esac
  fi
  case "${ARCH}" in
    silicon|intel) ARCHITECTURES=("${ARCH}") ;;
    all) ARCHITECTURES=(silicon intel) ;;
    *) echo "Expected native, silicon, intel, or all as the platform." >&2; exit 1 ;;
  esac
else
  if [[ "${ARCH}" != native ]]; then
    echo "Platform selection is currently supported only on macOS." >&2
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
    if [[ "${TARGET_ARCH}" == silicon ]]; then
      CMAKE_ARCH="arm64"
      PLATFORM_NAME="macos-silicon"
    else
      CMAKE_ARCH="x86_64"
      PLATFORM_NAME="macos-intel"
    fi
    TARGET_DIR="build/${PLATFORM_NAME}/${BUILD_TYPE}"
    CONFIGURE_ARGS+=("-DCMAKE_OSX_SYSROOT=${MACOS_SDK}" "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_ARCH}")
    if [[ "${ARCH}" == all || ( $# -ge 3 && -z "${2:-}" ) ]]; then
      PREFIX="${INSTALL_DIR}/${PLATFORM_NAME}"
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
