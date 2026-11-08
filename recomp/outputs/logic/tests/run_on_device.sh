#!/bin/bash
# Build the on-device tests and run them on the phone attached via adb.
# Usage: tests/run_on_device.sh [build_dir]
set -euo pipefail
cd "$(dirname "$0")/.."
NDK="${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/30.0.15729638}"
BUILD="${1:-build-android}"
cmake -S . -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=30 -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$BUILD" -j"$(nproc)" --target heap_test
adb push "$BUILD/heap_test" /data/local/tmp/grande_heap_test >/dev/null
adb shell 'chmod 755 /data/local/tmp/grande_heap_test; /data/local/tmp/grande_heap_test; echo "exit=$?"'
