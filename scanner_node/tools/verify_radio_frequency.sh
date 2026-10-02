#!/usr/bin/env bash
set -eu

cd "$(dirname "$0")/.."

echo "[1/10] Building scanner_node with both SDR backends..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic -pthread \
    -DCONFIG_PERF_ENABLE=1 -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -I../cJSON -Iperf_lib \
    src/main.c src/radio_worker.c src/analysis_worker.c src/device_config.c src/protocol.c src/udp_socket.c \
    src/radio_frontend.c src/dsp.c src/signal_classifier.c perf_lib/perf_probe.c perf_lib/perf_probe_hal_host.c \
    ../cJSON/cJSON.c \
    -lm -lbladeRF -lhackrf -o ./scanner_node

echo "[2/10] Running Python mock tests..."
python3 -m unittest discover -s tests -p test_demo_server_mock.py -v

echo "[3/10] Running C protocol tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic \
    -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -I../cJSON \
    tests/protocol_tests.c src/protocol.c src/device_config.c src/radio_frontend.c src/dsp.c \
    ../cJSON/cJSON.c -lm -lbladeRF -lhackrf -o ./scanner_protocol_tests
./scanner_protocol_tests ../device.json ./device-roundtrip.json

echo "[4/10] Running DSP PSD/classifier tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic \
    -Iinclude tests/dsp_tests.c src/dsp.c src/signal_classifier.c -lm -o ./scanner_dsp_tests
./scanner_dsp_tests

echo "[5/10] Running analysis worker tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic -pthread \
    -Iinclude tests/analysis_worker_tests.c src/analysis_worker.c src/dsp.c src/signal_classifier.c \
    -lm -o ./scanner_analysis_worker_tests
./scanner_analysis_worker_tests

echo "[6/10] Running radio worker tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic -pthread \
    -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -I../cJSON \
    tests/radio_worker_tests.c src/radio_worker.c src/radio_frontend.c src/dsp.c \
    -lm -lbladeRF -lhackrf -o ./scanner_radio_worker_tests
./scanner_radio_worker_tests

echo "[7/10] Building local BladeRF band-edge tool..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic -pthread \
    -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude \
    tools/bladerf_band_capture.c src/radio_frontend.c src/dsp.c src/signal_classifier.c \
    -lm -lbladeRF -lhackrf -o ./bladerf_band_capture

echo "[8/10] Building perf sweep benchmark..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -DCONFIG_PERF_ENABLE=1 -O1 -Wall -Wextra -Wpedantic -pthread \
    -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -Iperf_lib \
    tools/sdr_sweep_benchmark.c src/radio_frontend.c src/analysis_worker.c src/dsp.c \
    src/signal_classifier.c perf_lib/perf_probe.c perf_lib/perf_probe_hal_host.c \
    -lm -lbladeRF -lhackrf -o ./sdr_sweep_benchmark

echo "[9/10] Running UDP loopback tests..."
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Wpedantic \
    -Iinclude tests/udp_integration_tests.c src/protocol.c src/udp_socket.c \
    -o ./scanner_udp_integration_tests
./scanner_udp_integration_tests

echo "[10/10] Testing BladeRF then HackRF selection, frequency readback, neutral close, and Exit..."
python3 tools/radio_e2e_test.py --client ./scanner_node --device-json ../device.json --frontend-id 0
python3 tools/radio_e2e_test.py --client ./scanner_node --device-json ../device.json --frontend-id 1