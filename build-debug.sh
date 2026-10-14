#!/bin/sh
# Debug build of the PC port on Linux/macOS. Configures build-debug/ with
# CMake Debug (-g, no optimisation, full symbols) and builds it.
#
#   ./build-debug.sh              # build
#   ./build-debug.sh --reconfig   # wipe cache and reconfigure first
#
# Debug is the one to run under gdb / catch a crash with line numbers. It is
# unoptimised and slow; use build-release.sh for playtesting.
#
# Mirrors the flags build-release/ was configured with: GL3 backend, vendored
# SDL3, no wx configurator. Backends come from BFBB_RENDER_BACKENDS.
set -e
cd "$(dirname "$0")"

BUILD_DIR=build-debug
RENDER_BACKENDS=${BFBB_RENDER_BACKENDS:-GL3}

if [ "$1" = "--reconfig" ]; then
    rm -rf "$BUILD_DIR"
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBFBB_RENDER_BACKENDS="$RENDER_BACKENDS" \
        -DBFBB_SDL=vendored \
        -DBFBB_BUILD_CONFIGURATOR=OFF
fi

cmake --build "$BUILD_DIR"
echo "built: $BUILD_DIR/bfbb"
