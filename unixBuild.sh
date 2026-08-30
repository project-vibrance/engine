#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# Usage: ./unixBuild.sh [Debug|Release] [SDK install prefix]
# Anchoring to SCRIPT_DIR keeps calls from downstream repositories from
# configuring or installing the wrong source tree.
BUILD_TYPE=${1:-Debug}
if [[ "${BUILD_TYPE}" == "release" || "${BUILD_TYPE}" == "Release" ]]; then
  BUILD_TYPE="Release"
else
  BUILD_TYPE="Debug"
fi

INSTALL_DIR=${2:-"${SCRIPT_DIR}/install"}

mkdir -p build
echo "${BUILD_TYPE}" > build/.active_build

echo "Building vibrance-engine with configuration: ${BUILD_TYPE}"
echo "Installing vibrance-engine SDK to: ${INSTALL_DIR}"

TARGET_DIR="build/${BUILD_TYPE}"
mkdir -p "${TARGET_DIR}"

echo "Configuring engine CMake for ${BUILD_TYPE}..."
cmake -S . -B "${TARGET_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
cmake --build "${TARGET_DIR}" --config "${BUILD_TYPE}"
cmake --install "${TARGET_DIR}" --prefix "${INSTALL_DIR}" --config "${BUILD_TYPE}"
