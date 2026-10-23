#!/bin/sh
set -eu

build_only=0
compiler=${CC:-}
case "${1:-}" in
    '') ;;
    --build-only) build_only=1 ;;
    *) echo 'Usage: sh devtools/build_remote_stub.sh [--build-only]' >&2; exit 2 ;;
esac
test "$#" -le 1 || exit 2
cd "$(dirname "$0")/.."
if test -z "$compiler"; then
    if command -v clang >/dev/null 2>&1; then
        compiler=clang
    else
        compiler=gcc
    fi
fi
command -v "$compiler" >/dev/null
if test "$build_only" -eq 0; then
    command -v python3 >/dev/null
fi
mkdir -p build-remote
candidate=build-remote/scanner_node.new
object_dir=build-remote/objects
trap 'rm -f "$candidate"' EXIT

rm -rf "$object_dir"
mkdir -p "$object_dir"
"$compiler" -std=c99 -O0 -fno-inline -fno-builtin -I../cJSON \
    -c ../cJSON/cJSON.c -o "$object_dir/cJSON.o"
objects="$object_dir/cJSON.o"
for source in src/*.c perf_lib/perf_probe.c perf_lib/perf_probe_hal_host.c; do
    object="$object_dir/$(basename "$source" .c).o"
    "$compiler" -std=c99 -O0 -Wall -Wextra -Wpedantic -fno-inline -fno-builtin \
        -D_POSIX_C_SOURCE=200809L -DSCANNER_ENABLE_STUB_SDR=1 \
        -DCONFIG_PERF_ENABLE=1 -DCONFIG_PERF_STRINGS=1 -DCONFIG_PERF_LOG_ENABLE=1 \
        -Iinclude -Iperf_lib -I../cJSON -c "$source" -o "$object"
    objects="$objects $object"
done
"$compiler" $objects -pthread -lm -o "$candidate"

if test "$build_only" -eq 0; then
    "$compiler" -std=c99 -O0 -Wall -Wextra -Wpedantic -Iinclude -c tests/log_tests.c \
        -o build-remote/log_tests.o
    "$compiler" -std=c99 -O0 -Wall -Wextra -Wpedantic -Iinclude -c src/scanner_log.c \
        -o build-remote/log_module.o
    "$compiler" build-remote/log_tests.o build-remote/log_module.o -pthread \
        -o build-remote/scanner_log_tests
    ./build-remote/scanner_log_tests
    for name in radio_reinitialize commutator; do
        test_objects=""
        for source in "tests/${name}_tests.c" src/protocol.c src/device_config.c \
            src/radio_frontend.c src/radio_capture_backend.c src/dsp.c src/dsp_features.c \
            src/commutator.c src/scanner_log.c ../cJSON/cJSON.c; do
            object_name=$(printf '%s' "$source" | tr '/.' '__')
            object="build-remote/${name}_${object_name}.o"
            "$compiler" -std=c99 -O0 -Wall -Wextra -Wpedantic -fno-inline -fno-builtin \
                -D_POSIX_C_SOURCE=200809L -DSCANNER_ENABLE_STUB_SDR=1 \
                -Iinclude -I../cJSON -c "$source" -o "$object"
            test_objects="$test_objects $object"
        done
        "$compiler" $test_objects -pthread -lm -o "build-remote/scanner_${name}_tests"
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