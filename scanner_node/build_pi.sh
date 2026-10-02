#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/build-pi"}

needs_install=0
for tool in gcc cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        needs_install=1
    fi
done

for header in /usr/include/libbladeRF.h /usr/include/libhackrf/hackrf.h; do
    if [ ! -f "$header" ]; then
        needs_install=1
    fi
done

if [ "$needs_install" -eq 1 ]; then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "This build helper requires Debian/Ubuntu apt-get." >&2
        exit 1
    fi

    packages="build-essential cmake ninja-build pkg-config libusb-1.0-0-dev libbladerf-dev libhackrf-dev"
    if [ "$(id -u)" -eq 0 ]; then
        apt-get update
        apt-get install -y $packages
    else
        if ! command -v sudo >/dev/null 2>&1; then
            echo "Install dependencies as root or install sudo first." >&2
            exit 1
        fi
        sudo apt-get update
        sudo apt-get install -y $packages
    fi
fi

for tool in gcc cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required build tool is still missing: $tool" >&2
        exit 1
    fi
done

for header in /usr/include/libbladeRF.h /usr/include/libhackrf/hackrf.h; do
    if [ ! -f "$header" ]; then
        echo "Required SDR development header is still missing: $header" >&2
        exit 1
    fi
done

JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSCANNER_ENABLE_STUB_SDR=OFF \
    -DSCANNER_ENABLE_PERF=ON \
    -DBUILD_TESTING=OFF

for backend in BLADERF HACKRF; do
    if grep -q "^SCANNER_${backend}_LIBRARY:FILEPATH=.*NOTFOUND" "$BUILD_DIR/CMakeCache.txt"; then
        echo "CMake could not find the ${backend} library; install its development package." >&2
        exit 1
    fi
done

cmake --build "$BUILD_DIR" --parallel "$JOBS" \
    --target scanner_node scanner_sweep_benchmark

printf 'Build complete:\n  %s/scanner_node\n  %s/scanner_sweep_benchmark\n' \
    "$BUILD_DIR" "$BUILD_DIR"