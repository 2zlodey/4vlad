#!/usr/bin/env python3
"""Run scanner_node against the Python mock and verify frontend discovery/selection."""

import argparse
import socket
import subprocess
import threading
import time

from demo_server_mock import serve_one


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", default="./scanner_node", help="scanner executable")
    parser.add_argument("--device-json", default="../device.json", help="valid device JSON")
    parser.add_argument("--frontend-id", type=int, default=0, help="frontend to select")
    parser.add_argument("--timeout", type=float, default=90.0, help="overall client timeout in seconds")
    args = parser.parse_args()
    if not 0 <= args.frontend_id <= 255:
        parser.error("--frontend-id must be between 0 and 255")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

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
        "--attempts",
        "1",
        "--handshake-timeout",
        "3000",
        "--ver-timeout",
        "3000",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    timed_out = False
    try:
        deadline = time.monotonic() + args.timeout
        while process.poll() is None:
            if errors:
                process.kill()
                break
            if time.monotonic() >= deadline:
                timed_out = True
                process.kill()
                break
            time.sleep(0.05)
        stdout, stderr = process.communicate()
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
            or command_checks.get("sweep_points") != 2):
        raise SystemExit("radio settings, capture, measurement, or sweep command checks failed")
    print(
        "E2E PASS: {} frontend(s); selection starts id={} RX={} TX={} range={}..{} Hz frequency/settings/gain/power/raw-IQ/sweep=OK, neutral close=OK raw-IQ={}B IQ={}bit/fmt{} AGC=0x{:02x}".format(
            len(inventory["frontends"]),
            args.frontend_id,
            selected["rx_channels"],
            selected["tx_channels"],
            selected["frequency_min_hz"],
            selected["frequency_max_hz"],
            expected_raw_iq_bytes,
            selected["sample_resolution_bits"],
            selected["iq_sample_format"],
            selected["agc_modes"],
        )
    )


if __name__ == "__main__":
    main()