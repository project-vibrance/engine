#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${script_dir}"

# Usage: ./unixBuild.sh [Debug|Release] [installed engine SDK prefix]
build_type="${1:-Debug}"
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

engine_sdk="${2:-${VIBRANCE_ENGINE_ROOT:-}}"
if [[ -z "${engine_sdk}" && -f "../vibrance-engine/install/lib/cmake/vibrance_engine/vibrance_engineConfig.cmake" ]]; then
  engine_sdk="../vibrance-engine/install"
fi

if [[ -z "${engine_sdk}" ]]; then
  echo "Error: no installed vibrance-engine SDK was selected." >&2
  echo "Pass it as argument 2 or set VIBRANCE_ENGINE_ROOT." >&2
  echo "Example: ./unixBuild.sh Debug /opt/vibrance-sdk" >&2
  exit 2
fi
if [[ ! -f "${engine_sdk}/lib/cmake/vibrance_engine/vibrance_engineConfig.cmake" ]]; then
  echo "Error: ${engine_sdk} is not an installed vibrance-engine SDK prefix." >&2
  echo "Expected: ${engine_sdk}/lib/cmake/vibrance_engine/vibrance_engineConfig.cmake" >&2
  exit 2
fi

engine_sdk="$(cd -- "${engine_sdk}" && pwd)"
build_dir="build/${build_type}"
install_dir="${script_dir}/install"

echo "Configuring hello_vibrance ${build_type} against: ${engine_sdk}"
cmake -S . -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCMAKE_PREFIX_PATH="${engine_sdk}"
cmake --build "${build_dir}" --parallel
cmake --install "${build_dir}" --prefix "${install_dir}"

echo "Build and install completed."
echo "Run: ${install_dir}/bin/hello_vibrance"
