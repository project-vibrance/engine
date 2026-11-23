#!/usr/bin/env bash
# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

sdk=$(cd -- "$1" && pwd -P)
cache=$2
action=$3
assets="${sdk}/share/vibrance_engine"
patch="${assets}/patches/vibrance-kwin-liquid-glass-6.8.patch"
release=20261016-6.8.0-support
archive_hash=9c2a987eecf426e75117f4ef260619abadd151651eb756929e6fb25b5bfbf0a3

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

if [[ "${action}" == install ]]; then
  version=$(kwin_wayland --version)
  case "${version}" in
    'kwin 6.8'|'kwin 6.8.'*) ;;
    *) fail "Installing Vibrance Liquid Glass requires KWin 6.8. Found ${version}." ;;
  esac
fi

qtpaths=
if command -v qtpaths6 >/dev/null; then
  qtpaths=qtpaths6
elif [[ "$(qtpaths --qt-version 2>/dev/null)" == 6.* ]]; then
  qtpaths=qtpaths
fi

plugins=
cmake_args=()
if [[ -n "${qtpaths}" ]]; then
  plugins=$("${qtpaths}" --query QT_INSTALL_PLUGINS)
  qt_prefix=$("${qtpaths}" --query QT_INSTALL_PREFIX)
  if [[ "${qt_prefix}" == /* && "${plugins}" == "${qt_prefix%/}/"* ]]; then
    cmake_args+=("-DKDE_INSTALL_PLUGINDIR=${plugins#"${qt_prefix%/}/"}")
  fi
fi
if [[ "${action}" == install && ( "${plugins}" != /* || ! -d "${plugins}" ) ]]; then
  fail "Could not locate the system plug-in directory using Qt 6 qtpaths."
fi

mkdir -p "${cache}"
archive=${VIBRANCE_KWIN_GLASS_ARCHIVE:-"${cache}/source.tar.gz"}
source_dir="${cache}/source"
build_dir="${cache}/build"
work=$(mktemp -d "${cache}/.prepare.XXXXXX")
trap 'rm -rf -- "${work}"' EXIT

verify_archive() {
  local found
  found=$(sha256sum < "$1")
  [[ "${found%% *}" == "${archive_hash}" ]] || fail "Unexpected KWin Glass checksum for $1."
}

if [[ -z "${VIBRANCE_KWIN_GLASS_ARCHIVE:-}" && ! -f "${archive}" ]]; then
  curl --fail --location --silent --show-error --retry 2 \
    "https://codeload.github.com/4v3ngR/kwin-effects-glass/tar.gz/refs/tags/${release}" \
    --output "${work}/source.tar.gz"
  verify_archive "${work}/source.tar.gz"
  mv -- "${work}/source.tar.gz" "${archive}"
else
  verify_archive "${archive}"
fi

if [[ ! -f "${source_dir}/CMakeLists.txt" ]] || ! cmp -s -- "${patch}" "${source_dir}/.vibrance.patch"; then
  mkdir "${work}/source"
  tar -xzf "${archive}" --strip-components=1 -C "${work}/source"
  GIT_CEILING_DIRECTORIES="${cache}" git -C "${work}/source" apply "${patch}"
  cp -- "${patch}" "${work}/source/.vibrance.patch"
  rm -rf -- "${source_dir}"
  mv -- "${work}/source" "${source_dir}"
fi

if [[ ! -f "${build_dir}/CMakeCache.txt" && -z "${CMAKE_GENERATOR:-}" ]] && command -v ninja >/dev/null; then
  cmake_args+=(-G Ninja)
fi
if [[ -n "${VIBRANCE_KWIN_GLASS_CMAKE_PREFIX_PATH:-}" ]]; then
  cmake_args+=("-DCMAKE_PREFIX_PATH=${VIBRANCE_KWIN_GLASS_CMAKE_PREFIX_PATH}")
fi
cmake -S "${source_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${sdk}" \
  -DGLASS_WAYLAND=ON -DGLASS_X11=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF \
  "-DVIBRANCE_LIQUID_GLASS_PROTOCOL=${assets}/protocols/vibrance-liquid-glass-v1.xml" \
  "-DVIBRANCE_LIQUID_GLASS_PROFILE=${sdk}/include/vibranceUI/graphics/liquid_glass_refraction.h" \
  "${cmake_args[@]}"
cmake --build "${build_dir}" --config Release --target glass --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
cmake --install "${build_dir}" --config Release --prefix "${sdk}"

module=$(< "${build_dir}/install_manifest.txt")
[[ "${module}" == "${sdk}/"*/kwin/effects/plugins/vibrance_liquid_glass.so && -f "${module}" ]] ||
  fail "Could not locate the staged Vibrance Liquid Glass module."

if [[ "${action}" == install ]]; then
  destination="${plugins}/kwin/effects/plugins/${module##*/}"
  if cmp -s -- "${module}" "${destination}"; then
    printf 'Vibrance Liquid Glass is already installed at %s\n' "${destination}"
  else
    install_command=(install -D --backup=numbered -m 0755 -- "${module}" "${destination}")
    if (( EUID != 0 )); then
      install_command=(sudo "${install_command[@]}")
    fi
    "${install_command[@]}"
    printf 'Installed Vibrance Liquid Glass at %s\n' "${destination}"
    printf 'Sign out of Plasma and back in to load the updated module.\n'
  fi
fi
