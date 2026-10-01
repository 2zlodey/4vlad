#!/usr/bin/env bash
set -eu

cd "$(dirname "$0")/.."

echo "[1/5] Building scanner_node with both SDR backends..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic \
    -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -I../cJSON \
    src/main.c src/device_config.c src/protocol.c src/udp_socket.c src/radio_frontend.c \
    ../cJSON/cJSON.c -lm -lbladeRF -lhackrf -o ./scanner_node

echo "[2/5] Running Python mock tests..."
python3 -m unittest discover -s tests -p test_demo_server_mock.py -v

echo "[3/5] Running C protocol tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic \
    -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -I../cJSON \
    tests/protocol_tests.c src/protocol.c src/device_config.c src/radio_frontend.c \
    ../cJSON/cJSON.c -lm -lbladeRF -lhackrf -o ./scanner_protocol_tests
./scanner_protocol_tests ../device.json ./device-roundtrip.json

echo "[4/5] Running UDP loopback tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic \
    -Iinclude tests/udp_integration_tests.c src/protocol.c src/udp_socket.c \
    -o ./scanner_udp_integration_tests
./scanner_udp_integration_tests

echo "[5/5] Testing BladeRF then HackRF selection, frequency readback, neutral close, and Exit..."
python3 tools/radio_e2e_test.py --client ./scanner_node --device-json ../device.json --frontend-id 0
python3 tools/radio_e2e_test.py --client ./scanner_node --device-json ../device.json --frontend-id 1