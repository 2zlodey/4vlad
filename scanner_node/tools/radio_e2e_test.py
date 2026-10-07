#!/usr/bin/env python3
"""Run scanner_node against the Python mock and verify frontend discovery/selection."""

import argparse
import atexit
import os
import socket
import struct
import subprocess
import tempfile
import threading
import time

from demo_server_mock import serve_one


def validate_iq_recording(path, expected_records):
    with open(path, "rb") as recording:
        data = recording.read()
    if not data.startswith(b"SCIQREC1"):
        raise SystemExit("IQ recording has an invalid SCIQREC1 file header")

    offset = 8
    records = 0
    while offset < len(data):
        if len(data) - offset < 40 or data[offset : offset + 4] != b"IQRF":
            raise SystemExit("IQ recording contains a truncated or invalid record header")
        backend, channel, sample_format = data[offset + 4 : offset + 7]
        frequency_hz, sample_rate_hz, bandwidth_hz, complex_samples, payload_size, timestamp = struct.unpack_from(
            "<QIIIIQ", data, offset + 8
        )
        bytes_per_sample = 2 if sample_format == 1 else 4 if sample_format == 2 else 0
        if (backend not in (1, 2, 3) or sample_rate_hz == 0 or bandwidth_hz == 0 or frequency_hz == 0
                or complex_samples == 0 or timestamp == 0 or payload_size != complex_samples * bytes_per_sample):
            raise SystemExit("IQ recording record metadata is invalid")
        offset += 40 + payload_size
        if offset > len(data):
            raise SystemExit("IQ recording contains a truncated sample payload")
        records += 1
    if records != expected_records:
        raise SystemExit("IQ recording has {} records, expected {}".format(records, expected_records))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", default="./scanner_node", help="scanner executable")
    parser.add_argument("--device-json", default="../device.json", help="valid device JSON")
    parser.add_argument("--frontend-id", type=int, default=0, help="frontend to select")
    parser.add_argument("--timeout", type=float, default=90.0, help="overall client timeout in seconds")
    parser.add_argument("--debug", type=int, choices=range(4), default=1, help="scanner logging mode")
    args = parser.parse_args()
    if not 0 <= args.frontend_id <= 255:
        parser.error("--frontend-id must be between 0 and 255")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    iq_fd, iq_file = tempfile.mkstemp(prefix="scanner-e2e-", suffix=".iqrec")
    os.close(iq_fd)
    os.unlink(iq_file)
    atexit.register(lambda: os.path.exists(iq_file) and os.unlink(iq_file))

    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind(("127.0.0.1", 0))
    server.settimeout(min(args.timeout, 10.0))
    port = server.getsockname()[1]
    result = []
    errors = []

    def serve():
        try:
            result.append(serve_one(server, "1.0.0.0", min(args.timeout, 10.0), 1, True,
                                    args.frontend_id, True))
        except Exception as error:
            errors.append(error)

    worker = threading.Thread(target=serve)
    worker.start()
    command = [
        args.client,
        "--server",
        "127.0.0.1",
        "--server-port",
        str(port),
        "--local-port",
        "3333",
        "--device-json",
        args.device_json,
        "--iq-file",
        iq_file,
        "--attempts",
        "1",
        "--handshake-timeout",
        "3000",
        "--ver-timeout",
        "3000",
        "--debug",
        str(args.debug),
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    timed_out = False
    try:
        deadline = time.monotonic() + args.timeout
        while True:
            if errors:
                process.kill()
                stdout, stderr = process.communicate()
                break
            if time.monotonic() >= deadline:
                timed_out = True
                process.kill()
                stdout, stderr = process.communicate()
                break
            try:
                stdout, stderr = process.communicate(timeout=0.05)
                break
            except subprocess.TimeoutExpired:
                continue
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
        worker.join(timeout=12)
        server.close()

    print(stdout, end="")
    if stderr:
        print(stderr, end="")
    if worker.is_alive():
        raise SystemExit("Python mock server did not stop")
    if errors:
        raise SystemExit("Python mock server failed: {}".format(errors[0]))
    if timed_out:
        raise SystemExit("scanner_node exceeded the {:.1f}s E2E timeout".format(args.timeout))
    if process.returncode != 0:
        raise SystemExit("scanner_node exited with status {}".format(process.returncode))

    inventory = result[0][2]
    if inventory is None or not inventory["frontends"]:
        raise SystemExit("scanner_node reported no radio frontends")
    selected = next(
        (item for item in inventory["frontends"] if item["id"] == args.frontend_id), None
    )
    if selected is None or not selected["rx_channels"]:
        raise SystemExit("selected frontend does not advertise an RX channel")
    if not selected["capabilities"] & 0x01:
        raise SystemExit("selected frontend does not advertise generic RX capability")
    if selected["frequency_min_hz"] != 70000000:
        raise SystemExit("frontend does not use the common 70 MHz frequency floor")
    if selected["sample_resolution_bits"] not in (8, 12):
        raise SystemExit("frontend reported an unsupported ADC resolution")
    expected_format = 1 if selected["sample_resolution_bits"] == 8 else 2
    if selected["iq_sample_format"] != expected_format:
        raise SystemExit("frontend IQ format does not match its sample resolution")
    expected_frequencies = {str(item["id"]): 100000000 for item in inventory["frontends"]}
    if inventory.get("frequencies_hz") != expected_frequencies:
        raise SystemExit("set/get frequency round-trip failed for one or more frontends")
    if not inventory.get("neutral_closed"):
        raise SystemExit("neutral frontend selection did not close all radio handles")
    command_checks = inventory.get("commands_by_frontend", {}).get(str(args.frontend_id))
    expected_raw_iq_bytes = 64 * (2 if selected["iq_sample_format"] == 1 else 4)
    if (not command_checks or command_checks.get("raw_iq_bytes") != expected_raw_iq_bytes
            or command_checks.get("saved_iq_bytes") != expected_raw_iq_bytes
            or command_checks.get("sweep_points") != 2
            or "gain_stage_readback_supported" not in command_checks):
        raise SystemExit("radio settings, capture, measurement, or sweep command checks failed")
    recorded_count = validate_iq_recording(iq_file, len(inventory["frontends"]) * 6)
    print(
        "E2E PASS: {} frontend(s); selection starts id={} RX={} TX={} range={}..{} Hz frequency/settings/gain/power/raw-IQ/file-save/sweep=OK, neutral close=OK raw-IQ={}B records={} IQ={}bit/fmt{} AGC=0x{:02x}".format(
            len(inventory["frontends"]),
            args.frontend_id,
            selected["rx_channels"],
            selected["tx_channels"],
            selected["frequency_min_hz"],
            selected["frequency_max_hz"],
            expected_raw_iq_bytes,
            recorded_count,
            selected["sample_resolution_bits"],
            selected["iq_sample_format"],
            selected["agc_modes"],
        )
    )


if __name__ == "__main__":
    main()