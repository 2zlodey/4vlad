#!/usr/bin/env bash
set -eu

cd "$(dirname "$0")/.."

for tool in cmake ninja ctest python3 lsusb; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool not found: $tool" >&2
        exit 1
    fi
done

if [ ! -f ../cJSON/cJSON.c ] || [ ! -f ../cJSON/cJSON.h ]; then
    echo "Expected the shared cJSON dependency at ../cJSON" >&2
    exit 1
fi
if [ ! -f ../device.json ]; then
    echo "Expected the protocol test fixture at ../device.json" >&2
    exit 1
fi

echo "[1/5] Checking connected BladeRF and HackRF..."
devices=$(lsusb)
printf '%s\n' "$devices"
if ! printf '%s\n' "$devices" | grep -q '2cf0:5250'; then
    echo "BladeRF 2.0 micro (USB 2cf0:5250) is not connected" >&2
    exit 1
fi
if ! printf '%s\n' "$devices" | grep -q '1d50:6089'; then
    echo "HackRF One (USB 1d50:6089) is not connected" >&2
    exit 1
fi

BUILD_DIR=${BUILD_DIR:-build-radio-verify}
echo "[2/5] Configuring the canonical CMake build..."
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSCANNER_ENABLE_STUB_SDR=ON \
    -DSCANNER_ENABLE_PERF=ON \
    -DBUILD_TESTING=ON

for backend in BLADERF HACKRF; do
    if grep -q "^SCANNER_${backend}_LIBRARY:FILEPATH=.*NOTFOUND" "$BUILD_DIR/CMakeCache.txt"; then
        echo "CMake did not find the ${backend} development library" >&2
        exit 1
    fi
done

echo "[3/5] Building all CMake targets in parallel..."
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo "[4/5] Running CTest and Python mock tests..."
ctest --test-dir "$BUILD_DIR" --output-on-failure
python3 -m unittest discover -s tests -p test_demo_server_mock.py -v

echo "[5/5] Verifying both physical frontends through the UDP mock..."
python3 tools/radio_e2e_test.py --client "$BUILD_DIR/scanner_node" --device-json ../device.json --frontend-id 0
python3 tools/radio_e2e_test.py --client "$BUILD_DIR/scanner_node" --device-json ../device.json --frontend-id 1

echo "Radio verification passed."