#!/usr/bin/env python3
"""Capture one IQ window from one selected radio into a new SCIQREC1 file."""

import argparse
import socket
import struct
import subprocess
from pathlib import Path
from demo_server_mock import (
    SET_BANDWIDTH_COMMAND,
    SET_FREQUENCY_COMMAND,
    SET_SAMPLE_RATE_COMMAND,
    build_exit_request,
    build_radio_frontends_request,
    build_save_iq_request,
    build_set_active_radio_request,
    build_set_frequency_request,
    build_set_u32_request,
    build_session_reply,
    build_ver_request,
    next_request_id,
    parse_handshake,
    parse_radio_frontends_response,
    validate_exit_response,
    validate_frequency_response,
    validate_iq_file_response,
    validate_setting_response,
    validate_set_active_radio_response,
    validate_ver_response,
)


def receive_from(sock, expected_source):
    payload, source = sock.recvfrom(65535)
    if source != expected_source:
        raise RuntimeError("unexpected UDP source {}".format(source))
    return payload


def exchange(sock, client_endpoint, client_source, request):
    sock.sendto(request, client_endpoint)
    return receive_from(sock, client_source)


def validate_record(path, expected_backend, expected_format, expected_samples):
    data = Path(path).read_bytes()
    if not data.startswith(b"SCIQREC1") or len(data) < 48:
        raise RuntimeError("recording is missing SCIQREC1 file/record header")
    if data[8:12] != b"IQRF":
        raise RuntimeError("recording has an invalid record marker")
    backend, channel, sample_format = data[12:15]
    frequency_hz, sample_rate_hz, bandwidth_hz, sample_count, payload_size, timestamp = struct.unpack_from(
        "<QIIIIQ", data, 16
    )
    expected_payload_size = expected_samples * (2 if expected_format == 1 else 4)
    if (backend, channel, sample_format, sample_count, payload_size) != (
        expected_backend, 0, expected_format, expected_samples, expected_payload_size
    ):
        raise RuntimeError("recording metadata does not match selected frontend/capture")
    if frequency_hz == 0 or sample_rate_hz == 0 or bandwidth_hz == 0 or timestamp == 0:
        raise RuntimeError("recording is missing tune/configuration/timestamp metadata")
    if len(data) != 48 + payload_size:
        raise RuntimeError("expected exactly one complete IQ record, got {} bytes".format(len(data)))
    return frequency_hz, sample_rate_hz, bandwidth_hz, payload_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", required=True, help="scanner_node executable")
    parser.add_argument("--device-json", required=True, help="device configuration JSON")
    parser.add_argument("--frontend-id", required=True, type=int, choices=(0, 1),
                        help="0=BladeRF, 1=HackRF in this project's discovery order")
    parser.add_argument("--output", required=True, help="new output .iqrec file; existing files are not overwritten")
    parser.add_argument("--frequency-mhz", type=int, default=1000)
    parser.add_argument("--sample-rate-hz", type=int, default=2000000)
    parser.add_argument("--bandwidth-hz", type=int, default=1750000)
    parser.add_argument("--samples", type=int, default=4096)
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    output = Path(args.output).expanduser().resolve()
    if output.exists():
        parser.error("output already exists; choose a new path: {}".format(output))
    if not 1 <= args.samples <= 4096 or args.timeout <= 0 or args.frequency_mhz < 70:
        parser.error("invalid sample count, timeout, or frequency")
    output.parent.mkdir(parents=True, exist_ok=True)

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(("127.0.0.1", 0))
        server.settimeout(args.timeout)
        server_port = server.getsockname()[1]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port_probe:
            port_probe.bind(("127.0.0.1", 0))
            local_port = port_probe.getsockname()[1]

        process = None
        try:
            process = subprocess.Popen([
                args.client, "--server", "127.0.0.1", "--server-port", str(server_port),
                "--local-port", str(local_port), "--device-json", args.device_json,
                "--iq-file", str(output), "--attempts", "1", "--handshake-timeout", "5000",
                "--ver-timeout", "5000",
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

            handshake_bytes, source = server.recvfrom(65535)
            handshake = parse_handshake(handshake_bytes)
            client_endpoint = (source[0], handshake["reply_port"])
            server.sendto(build_session_reply(), client_endpoint)
            request_id = 1
            server.sendto(build_ver_request(request_id), client_endpoint)
            validate_ver_response(receive_from(server, source), request_id, "1.0.0.0")

            request_id = next_request_id(request_id)
            frontends = parse_radio_frontends_response(
                exchange(server, client_endpoint, source, build_radio_frontends_request(request_id)), request_id
            )["frontends"]
            frontend = next((item for item in frontends if item["id"] == args.frontend_id), None)
            if frontend is None:
                raise RuntimeError("requested frontend {} was not discovered".format(args.frontend_id))
            expected_backend, expected_format, frontend_name = (
                (1, 2, "BladeRF") if args.frontend_id == 0 else (2, 1, "HackRF")
            )
            if frontend["iq_sample_format"] != expected_format:
                raise RuntimeError("frontend ID {} is not the expected physical SDR".format(args.frontend_id))

            request_id = next_request_id(request_id)
            response = exchange(server, client_endpoint, source,
                                build_set_active_radio_request(request_id, args.frontend_id))
            validate_set_active_radio_response(response, request_id, args.frontend_id)

            sample_rate = min(max(args.sample_rate_hz, frontend["sample_rate_min_hz"]),
                              frontend["sample_rate_max_hz"])
            request_id = next_request_id(request_id)
            response = exchange(server, client_endpoint, source,
                                build_set_u32_request(request_id, SET_SAMPLE_RATE_COMMAND, 0, sample_rate))
            applied_sample_rate = struct.unpack_from("<I", response, 7)[0]
            validate_setting_response(response, request_id, SET_SAMPLE_RATE_COMMAND, 0, applied_sample_rate)

            bandwidth = args.bandwidth_hz
            if frontend["bandwidth_options"]:
                choices = frontend["bandwidth_options"]
                bandwidth = min(choices, key=lambda value: abs(value - args.bandwidth_hz))
            bandwidth = min(max(bandwidth, frontend["bandwidth_min_hz"]), frontend["bandwidth_max_hz"])
            request_id = next_request_id(request_id)
            response = exchange(server, client_endpoint, source,
                                build_set_u32_request(request_id, SET_BANDWIDTH_COMMAND, 0, bandwidth))
            applied_bandwidth = struct.unpack_from("<I", response, 7)[0]
            validate_setting_response(response, request_id, SET_BANDWIDTH_COMMAND, 0, applied_bandwidth)

            frequency_khz = args.frequency_mhz * 1000
            request_id = next_request_id(request_id)
            response = exchange(server, client_endpoint, source,
                                build_set_frequency_request(request_id, 0, frequency_khz))
            applied_frequency_hz = struct.unpack_from("<Q", response, 7)[0]
            validate_frequency_response(response, request_id, SET_FREQUENCY_COMMAND, 0, applied_frequency_hz)

            request_id = next_request_id(request_id)
            response = exchange(server, client_endpoint, source,
                                build_save_iq_request(request_id, 0, args.samples))
            payload_size = validate_iq_file_response(response, request_id, 0,
                                                      frontend["iq_sample_format"], args.samples)

            request_id = next_request_id(request_id)
            validate_exit_response(exchange(server, client_endpoint, source, build_exit_request(request_id)),
                                   request_id)
            stdout, stderr = process.communicate(timeout=args.timeout)
            if process.returncode != 0:
                raise RuntimeError("scanner_node failed ({}): {}".format(process.returncode, stderr.strip()))

            actual_frequency_hz, actual_sample_rate, actual_bandwidth, actual_payload_size = validate_record(
                output, expected_backend, expected_format, args.samples
            )
            if actual_frequency_hz != applied_frequency_hz:
                raise RuntimeError("recorded frequency does not match SET_FREQUENCY readback")
            if actual_sample_rate != applied_sample_rate:
                raise RuntimeError("recorded sample rate does not match configured rate")
            if actual_bandwidth != applied_bandwidth:
                raise RuntimeError("recorded bandwidth does not match configured bandwidth")
            if actual_payload_size != payload_size:
                raise RuntimeError("recorded payload size does not match SAVE_IQ_TO_FILE acknowledgement")
        except Exception:
            if process is not None and process.poll() is None:
                process.kill()
                process.communicate()
            raise

    print("Saved one {} IQ record: {}".format(frontend_name, output))
    print("frequency={} Hz sample_rate={} Hz bandwidth={} Hz samples={} format={} bytes={}".format(
        actual_frequency_hz, actual_sample_rate, actual_bandwidth, args.samples,
        frontend["iq_sample_format"], payload_size
    ))


if __name__ == "__main__":
    main()
