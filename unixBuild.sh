#!/bin/bash
set -e

BUILD_TYPE=${1:-Debug}
if [ "$1" == "release" ] || [ "$1" == "Release" ]; then
  BUILD_TYPE="Release"
fi

INSTALL_DIR=${2:-"$(pwd)/install"}

mkdir -p build
echo "${BUILD_TYPE}" > build/.active_build

echo "Building with configuration: ${BUILD_TYPE}"
echo "Installing vibrance-engine SDK to: ${INSTALL_DIR}"

TARGET_DIR="build/${BUILD_TYPE}"
mkdir -p "${TARGET_DIR}"

echo "Configuring CMake for ${BUILD_TYPE}..."
cmake -S . -B "${TARGET_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
cmake --build "${TARGET_DIR}" --config "${BUILD_TYPE}"
cmake --install "${TARGET_DIR}" --prefix "${INSTALL_DIR}" --config "${BUILD_TYPE}"
