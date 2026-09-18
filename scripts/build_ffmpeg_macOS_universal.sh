#!/usr/bin/env bash
set -euo pipefail

FFMPEG_VERSION="8.1.2"

SOURCE_DIR="${1:-$HOME/src/ffmpeg-$FFMPEG_VERSION}"
OUTPUT_ROOT="${2:-$HOME/dev}"
BUILD_ROOT="${3:-$HOME/build}"

# Minimum macOS version supported by the produced libraries.
MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-12.0}"

HOST_ARCH="$(uname -m)"

# ============================================================
# Verify Xcode / macOS SDK
# ============================================================

if ! command -v xcrun >/dev/null 2>&1; then
    echo "error: xcrun was not found."
    echo "Install Xcode or Xcode Command Line Tools."
    exit 1
fi

if ! xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1; then
    echo "error: macOS SDK could not be located."
    echo "Check your Xcode installation and xcode-select configuration."
    exit 1
fi

SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SDK_VERSION="$(xcrun --sdk macosx --show-sdk-version)"

CC="$(xcrun --sdk macosx --find clang)"
CXX="$(xcrun --sdk macosx --find clang++)"
AR="$(xcrun --sdk macosx --find ar)"
RANLIB="$(xcrun --sdk macosx --find ranlib)"
STRIP="$(xcrun --sdk macosx --find strip)"
LIPO="$(xcrun --sdk macosx --find lipo)"

HOST_CC="$CC"
HOST_LD="$CC"

echo
echo "============================================================"
echo "FFmpeg $FFMPEG_VERSION macOS build"
echo "============================================================"
echo
echo "Host architecture:       $HOST_ARCH"
echo "macOS SDK version:       $SDK_VERSION"
echo "SDK root:                $SDKROOT"
echo "Deployment target:       macOS $MACOS_DEPLOYMENT_TARGET"
echo "Target compiler:         $CC"
echo "Host compiler:           $HOST_CC"
echo

# ============================================================
# Verify FFmpeg source
# ============================================================

if [[ ! -f "$SOURCE_DIR/configure" ]]; then
    echo "error: FFmpeg source not found:"
    echo "  $SOURCE_DIR"
    exit 1
fi

# ============================================================
# Verify SDK
# ============================================================

if [[ ! -d "$SDKROOT/usr/include" ]]; then
    echo "error: macOS SDK include directory was not found:"
    echo "  $SDKROOT/usr/include"
    exit 1
fi

# ============================================================
# Verify host compiler C11/C17 support
# ============================================================

echo "Checking host compiler C11/C17 support..."

C_TEST_SOURCE="$(mktemp /tmp/ffmpeg-c-test.XXXXXX)"
C_TEST_BINARY="${C_TEST_SOURCE}.bin"

cleanup_c_test() {
    rm -f "$C_TEST_SOURCE" "$C_TEST_BINARY"
}

trap cleanup_c_test EXIT

cat > "$C_TEST_SOURCE" <<'EOF'
#include <stdio.h>
#include <stdlib.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error C11 or newer is required
#endif

int main(void)
{
    printf(
        "Host compiler C standard: %ld\n",
        (long)__STDC_VERSION__
    );

    return EXIT_SUCCESS;
}
EOF

"$HOST_CC" \
    -x c \
    -std=c17 \
    -isysroot "$SDKROOT" \
    -mmacosx-version-min="$MACOS_DEPLOYMENT_TARGET" \
    "$C_TEST_SOURCE" \
    -o "$C_TEST_BINARY"

"$C_TEST_BINARY"

cleanup_c_test
trap - EXIT

echo
echo "Host compiler check passed."
echo

# ============================================================
# Build each architecture
# ============================================================

for TARGET_ARCH in arm64 x86_64; do

    PREFIX="$OUTPUT_ROOT/ffmpeg-$FFMPEG_VERSION-lgpl-static-darwin-$TARGET_ARCH"
    BUILD_DIR="$BUILD_ROOT/ffmpeg-$FFMPEG_VERSION-darwin-$TARGET_ARCH"

    echo
    echo "============================================================"
    echo "Building FFmpeg $FFMPEG_VERSION"
    echo "============================================================"
    echo
    echo "Target architecture:     $TARGET_ARCH"
    echo "Host architecture:       $HOST_ARCH"
    echo "SDK:                     macOS $SDK_VERSION"
    echo "Deployment target:       macOS $MACOS_DEPLOYMENT_TARGET"
    echo "Install prefix:          $PREFIX"
    echo "Build directory:         $BUILD_DIR"
    echo

    # --------------------------------------------------------
    # Clean previous build
    # --------------------------------------------------------

    rm -rf "$BUILD_DIR"
    rm -rf "$PREFIX"

    mkdir -p "$BUILD_DIR"
    mkdir -p "$PREFIX"

    cd "$BUILD_DIR"

    export MACOSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"

    # --------------------------------------------------------
    # Common target flags
    # --------------------------------------------------------

    TARGET_CFLAGS=(
        "-arch" "$TARGET_ARCH"
        "-isysroot" "$SDKROOT"
        "-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"
    )

    TARGET_LDFLAGS=(
        "-arch" "$TARGET_ARCH"
        "-isysroot" "$SDKROOT"
        "-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"
    )

    # Host tools run on the current Mac, so use the host
    # architecture rather than TARGET_ARCH.
    HOST_CFLAGS=(
        "-arch" "$HOST_ARCH"
        "-isysroot" "$SDKROOT"
        "-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"
    )

    HOST_LDFLAGS=(
        "-arch" "$HOST_ARCH"
        "-isysroot" "$SDKROOT"
        "-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"
    )

    # --------------------------------------------------------
    # Configure
    # --------------------------------------------------------

    CONFIGURE_ARGS=(
        "$SOURCE_DIR/configure"

        # Installation
        "--prefix=$PREFIX"

        # Platform
        "--target-os=darwin"
        "--arch=$TARGET_ARCH"
        "--sysroot=$SDKROOT"

        # ----------------------------------------------------
        # Target toolchain
        # ----------------------------------------------------

        "--cc=$CC"
        "--cxx=$CXX"
        "--ar=$AR"
        "--ranlib=$RANLIB"
        "--strip=$STRIP"

        # ----------------------------------------------------
        # Host toolchain
        #
        # These executables run during the FFmpeg build itself.
        # They must be built for HOST_ARCH and must have access
        # to the macOS SDK.
        # ----------------------------------------------------

        "--host-cc=$HOST_CC"
        "--host-ld=$HOST_LD"

        "--host-cflags=${HOST_CFLAGS[*]}"
        "--host-cppflags=-isysroot $SDKROOT"
        "--host-ldflags=${HOST_LDFLAGS[*]}"

        # ----------------------------------------------------
        # Language standards
        # ----------------------------------------------------

        "--stdc=c17"
        "--stdcxx=c++17"

        # ----------------------------------------------------
        # Static libraries
        # ----------------------------------------------------

        "--enable-static"
        "--disable-shared"
        "--enable-pic"

        # ----------------------------------------------------
        # Licensing
        # ----------------------------------------------------

        "--disable-gpl"
        "--disable-nonfree"
        "--disable-version3"

        # ----------------------------------------------------
        # Minimal dependency setup
        # ----------------------------------------------------

        "--disable-autodetect"
        "--disable-debug"
        "--disable-doc"
        "--disable-programs"

        "--disable-avdevice"
        "--disable-avfilter"

        # ----------------------------------------------------
        # Target flags
        # ----------------------------------------------------

        "--extra-cflags=${TARGET_CFLAGS[*]}"
        "--extra-cxxflags=${TARGET_CFLAGS[*]}"
        "--extra-ldflags=${TARGET_LDFLAGS[*]}"
    )

    # --------------------------------------------------------
    # Cross compilation
    # --------------------------------------------------------

    if [[ "$HOST_ARCH" != "$TARGET_ARCH" ]]; then

        echo "Cross-compiling:"
        echo "  $HOST_ARCH -> $TARGET_ARCH"
        echo

        CONFIGURE_ARGS+=(
            "--enable-cross-compile"
        )

    fi

    # --------------------------------------------------------
    # Run configure
    # --------------------------------------------------------

    "${CONFIGURE_ARGS[@]}"

    # --------------------------------------------------------
    # Build
    # --------------------------------------------------------

    CPU_COUNT="$(sysctl -n hw.ncpu)"

    echo
    echo "Compiling with $CPU_COUNT threads..."
    echo

    make -j"$CPU_COUNT"

    # --------------------------------------------------------
    # Install
    # --------------------------------------------------------

    make install

    # --------------------------------------------------------
    # Verify libraries
    # --------------------------------------------------------

    echo
    echo "Verifying generated libraries..."
    echo

    REQUIRED_LIBS=(
        "libavcodec.a"
        "libavformat.a"
        "libavutil.a"
        "libswresample.a"
        "libswscale.a"
    )

    for LIB in "${REQUIRED_LIBS[@]}"; do

        LIB_PATH="$PREFIX/lib/$LIB"

        if [[ ! -f "$LIB_PATH" ]]; then

            echo "error: expected library not found:"
            echo "  $LIB_PATH"

            exit 1

        fi

        "$LIPO" -info "$LIB_PATH"

    done

    echo
    echo "FFmpeg $TARGET_ARCH build completed successfully."
    echo

done

# ============================================================
# Finished
# ============================================================

echo
echo "============================================================"
echo "FFmpeg builds completed successfully"
echo "============================================================"
echo
echo "ARM64:"
echo "  $OUTPUT_ROOT/ffmpeg-$FFMPEG_VERSION-lgpl-static-darwin-arm64"
echo
echo "x86_64:"
echo "  $OUTPUT_ROOT/ffmpeg-$FFMPEG_VERSION-lgpl-static-darwin-x86_64"
echo