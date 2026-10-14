#!/bin/sh
# Release build of the PC port on Linux/macOS. Configures build-release/ with
# CMake Release (-O3) and builds it. This is the playtest build.
#
#   ./build-release.sh              # build
#   ./build-release.sh --reconfig   # wipe cache and reconfigure first
#
# Debug builds for crashes/gdb live in build-debug.sh. Backends come from
# BFBB_RENDER_BACKENDS.
set -e
cd "$(dirname "$0")"

BUILD_DIR=build-release
RENDER_BACKENDS=${BFBB_RENDER_BACKENDS:-GL3}

if [ "$1" = "--reconfig" ]; then
    rm -rf "$BUILD_DIR"
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBFBB_RENDER_BACKENDS="$RENDER_BACKENDS" \
        -DBFBB_SDL=vendored \
        -DBFBB_BUILD_CONFIGURATOR=OFF
fi

cmake --build "$BUILD_DIR"
echo "built: $BUILD_DIR/bfbb"
