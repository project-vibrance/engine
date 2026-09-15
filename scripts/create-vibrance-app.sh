#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $(basename "$0") TARGET_DIRECTORY ENGINE_SDK [Debug|Release]"
  echo "Example: $(basename "$0") /home/me/hello-vibrance /opt/vibrance-sdk Debug"
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
template_dir="${script_dir}/templates/vibrance-app"
target_dir="$1"
sdk_dir="$2"
build_type="${3:-Debug}"

case "${build_type}" in
  debug|Debug)
    build_type="Debug"
    ;;
  release|Release)
    build_type="Release"
    ;;
  *)
    echo "Error: build type must be Debug or Release." >&2
    exit 2
    ;;
esac

if [[ ! -f "${template_dir}/CMakeLists.txt" ]]; then
  echo "Error: starter template is missing: ${template_dir}" >&2
  exit 2
fi
if [[ ! -f "${sdk_dir}/lib/cmake/vibrance_engine/vibrance_engineConfig.cmake" ]]; then
  echo "Error: ${sdk_dir} is not an installed vibrance-engine SDK prefix." >&2
  echo "Expected: ${sdk_dir}/lib/cmake/vibrance_engine/vibrance_engineConfig.cmake" >&2
  exit 2
fi

mkdir -p -- "${target_dir}"
if [[ -n "$(ls -A "${target_dir}")" ]]; then
  echo "Error: target directory must be empty: ${target_dir}" >&2
  exit 2
fi

target_dir="$(cd -- "${target_dir}" && pwd)"
sdk_dir="$(cd -- "${sdk_dir}" && pwd)"
build_dir="${target_dir}/build/${build_type}"
install_dir="${target_dir}/install"

echo "Creating the starter project in: ${target_dir}"
cmake -E copy_directory "${template_dir}" "${target_dir}"

echo "Configuring ${build_type} against: ${sdk_dir}"
cmake -S "${target_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCMAKE_PREFIX_PATH="${sdk_dir}"
cmake --build "${build_dir}" --parallel
cmake --install "${build_dir}" --prefix "${install_dir}"

echo
echo "Starter application created successfully."
echo "Source: ${target_dir}"
echo "Run:    ${install_dir}/bin/hello_vibrance"
