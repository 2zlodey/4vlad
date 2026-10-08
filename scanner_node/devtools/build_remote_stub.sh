#!/bin/sh
set -eu

build_only=0
case "${1:-}" in
    '') ;;
    --build-only) build_only=1 ;;
    *) echo 'Usage: sh devtools/build_remote_stub.sh [--build-only]' >&2; exit 2 ;;
esac
test "$#" -le 1 || exit 2
cd "$(dirname "$0")/.."
command -v gcc >/dev/null
if test "$build_only" -eq 0; then
    command -v python3 >/dev/null
fi
mkdir -p build-remote
candidate=build-remote/scanner_node.new
trap 'rm -f "$candidate"' EXIT

gcc -std=c99 -O2 -Wall -Wextra -Wpedantic \
    -D_POSIX_C_SOURCE=200809L -DSCANNER_ENABLE_STUB_SDR=1 \
    -DCONFIG_PERF_ENABLE=1 -DCONFIG_PERF_STRINGS=1 -DCONFIG_PERF_LOG_ENABLE=1 \
    -Iinclude -Iperf_lib -I../cJSON src/*.c \
    perf_lib/perf_probe.c perf_lib/perf_probe_hal_host.c ../cJSON/cJSON.c \
    -pthread -lm -o "$candidate"

if test "$build_only" -eq 0; then
    gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -Iinclude \
        tests/log_tests.c src/scanner_log.c -pthread -o build-remote/scanner_log_tests
    ./build-remote/scanner_log_tests
    for name in radio_reinitialize commutator; do
        gcc -std=c99 -O2 -Wall -Wextra -Wpedantic \
            -D_POSIX_C_SOURCE=200809L -DSCANNER_ENABLE_STUB_SDR=1 \
            -Iinclude -I../cJSON "tests/${name}_tests.c" \
            src/protocol.c src/device_config.c src/radio_frontend.c src/radio_capture_backend.c \
            src/dsp.c src/dsp_features.c src/commutator.c src/scanner_log.c ../cJSON/cJSON.c \
            -pthread -lm -o "build-remote/scanner_${name}_tests"
        "./build-remote/scanner_${name}_tests"
    done
    python3 tools/logging_e2e_test.py --client "$candidate" --device-json ../device.json
fi

mv "$candidate" build-remote/scanner_node
echo "Stub build installed: $(pwd)/build-remote/scanner_node"
if test "$build_only" -eq 1; then
    echo 'WARNING: runtime checks skipped (--build-only).'
else
    echo 'Logging, SDR reinitialization, commutator and four-mode UDP E2E checks passed.'
fi