#!/bin/sh
set -eu

packages="build-essential clang cmake ninja-build python3 python3-venv pkg-config libusb-1.0-0-dev libhackrf-dev libbladerf-dev"
build_only=0
case "${1:-}" in
    '') ;;
    --build-only) build_only=1 ;;
    *) echo 'Usage: sh devtools/setup_conductor.sh [--build-only]' >&2; exit 2 ;;
esac
test "$#" -le 1 || exit 2
missing=""
for package in $packages; do
    if ! dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed'; then
        missing="$missing $package"
    fi
done

if test -n "$missing"; then
    if test "$(id -u)" -eq 0; then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y $missing
    else
        sudo apt-get update
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y $missing
    fi
fi

cd "$(dirname "$0")/.."
if test "$build_only" -eq 1; then
    sh devtools/build_remote_stub.sh --build-only
else
    sh devtools/build_remote_stub.sh
fi
