#!/usr/bin/env python3
"""Capture real RX power sweeps from the Pi's SDRs and save a 10 MHz CSV grid."""

import argparse
import csv
import datetime
import pathlib
import shlex
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from demo_server_mock import (  # noqa: E402
    EXIT_COMMAND,
    GET_GAIN_COMMAND,
    RADIO_STATUS_OK,
    SET_ACTIVE_RADIO_COMMAND,
    SET_BANDWIDTH_COMMAND,
    SET_LNA_GAIN_COMMAND,
    SET_SAMPLE_RATE_COMMAND,
    SET_VGA_GAIN_COMMAND,
    SWEEP_COMMAND,
    build_exit_request,
    build_get_value_request,
    build_radio_frontends_request,
    build_session_reply,
    build_set_active_radio_request,
    build_set_gain_request,
    build_ver_request,
    next_request_id,
    parse_handshake,
    parse_radio_frontends_response,
    validate_exit_response,
    validate_gain_stage_response,
    validate_set_active_radio_response,
    validate_ver_response,
)


def receive_from_pi(sock, pi_address, timeout):
    sock.settimeout(timeout)
    payload, source = sock.recvfrom(65535)
    if source[0] != pi_address:
        raise RuntimeError("received UDP packet from unexpected host {}".format(source[0]))
    return payload, source


def transact(sock, client_endpoint, pi_address, packet, timeout=15):
    sock.sendto(packet, client_endpoint)
    payload, _ = receive_from_pi(sock, pi_address, timeout)
    return payload


def expect_radio_header(payload, request_id, command, expected_size=None):
    if len(payload) < 6:
        raise RuntimeError("short response for command 0x{:02x}".format(command))
    actual_id, actual_command, status = struct.unpack_from("<IBB", payload)
    if actual_id != request_id or actual_command != command:
        raise RuntimeError("response id/opcode mismatch for command 0x{:02x}".format(command))
    if status != RADIO_STATUS_OK:
        raise RuntimeError("command 0x{:02x} failed with status {}".format(command, status))
    if expected_size is not None and len(payload) != expected_size:
        raise RuntimeError("command 0x{:02x} response length {} != {}".format(
            command, len(payload), expected_size
        ))


def build_setting_request(request_id, command, channel, value):
    return struct.pack("<IBBI", request_id, command, channel, value)


def validate_setting(payload, request_id, command, channel, requested_value):
    expect_radio_header(payload, request_id, command, 11)
    _, _, _, actual_channel, actual_value = struct.unpack("<IBBBI", payload)
    if actual_channel != channel or actual_value == 0:
        raise RuntimeError("invalid setting readback from command 0x{:02x}".format(command))
    if command == SET_SAMPLE_RATE_COMMAND and actual_value != requested_value:
        raise RuntimeError("sample-rate readback differs from request")
    return actual_value


def build_sweep_request(request_id, channel, start_khz, stop_khz, step_khz):
    return struct.pack("<IBBIII", request_id, SWEEP_COMMAND, channel,
                       start_khz, stop_khz, step_khz)


def parse_sweep_response(payload, request_id, channel, expected_count):
    if len(payload) < 9:
        raise RuntimeError("short sweep response")
    actual_id, command, status, actual_channel, count = struct.unpack_from("<IBBBH", payload)
    if actual_id != request_id or command != SWEEP_COMMAND or actual_channel != channel:
        raise RuntimeError("sweep response header mismatch")
    if status != RADIO_STATUS_OK:
        raise RuntimeError("sweep failed with status {}".format(status))
    if count != expected_count or len(payload) != 9 + count * 6:
        raise RuntimeError("sweep point count/response length mismatch")
    return [struct.unpack_from("<Ih", payload, 9 + index * 6) for index in range(count)]


def connect_scanner(sock, pi_address, pi_user, key_file, windows_address, port,
                    binary, device_json, timeout):
    handshake, source = receive_from_pi(sock, pi_address, timeout)
    parsed = parse_handshake(handshake)
    client_endpoint = (source[0], parsed["reply_port"])
    sock.sendto(build_session_reply(), client_endpoint)
    sock.sendto(build_ver_request(1), client_endpoint)
    ver_response, _ = receive_from_pi(sock, pi_address, timeout)
    validate_ver_response(ver_response, 1, "1.0.0.0")

    request_id = 2
    response = transact(sock, client_endpoint, pi_address,
                        build_radio_frontends_request(request_id), timeout)
    inventory = parse_radio_frontends_response(response, request_id)
    if not inventory["frontends"]:
        raise RuntimeError("scanner reported no RX frontends")
    return client_endpoint, inventory


def scan_one_frontend(sock, client_endpoint, pi_address, frontend_id, frontend_name,
                      start_khz, stop_khz, step_khz, timeout,
                      hackrf_lna_gain_db, hackrf_vga_gain_db):
    request_id = 3
    response = transact(sock, client_endpoint, pi_address,
                        build_set_active_radio_request(request_id, frontend_id), timeout)
    validate_set_active_radio_response(response, request_id, frontend_id)

    if frontend_name == "hackrf_one":
        request_id = next_request_id(request_id)
        response = transact(
            sock, client_endpoint, pi_address,
            build_set_gain_request(request_id, SET_LNA_GAIN_COMMAND, 0, hackrf_lna_gain_db), timeout
        )
        validate_gain_stage_response(response, request_id, SET_LNA_GAIN_COMMAND, 0, hackrf_lna_gain_db)
        request_id = next_request_id(request_id)
        response = transact(
            sock, client_endpoint, pi_address,
            build_set_gain_request(request_id, SET_VGA_GAIN_COMMAND, 0, hackrf_vga_gain_db), timeout
        )
        validate_gain_stage_response(response, request_id, SET_VGA_GAIN_COMMAND, 0, hackrf_vga_gain_db)

    request_id = next_request_id(request_id)
    response = transact(sock, client_endpoint, pi_address,
                        build_get_value_request(request_id, GET_GAIN_COMMAND, 0), timeout)
    expect_radio_header(response, request_id, GET_GAIN_COMMAND, 9)
    _, _, _, gain_channel, total_gain_cdb = struct.unpack("<IBBBh", response)
    if gain_channel != 0:
        raise RuntimeError("gain readback channel mismatch")

    # Both attached radios accept 2 MS/s and 1.75 MHz RX bandwidth.
    request_id = next_request_id(request_id)
    response = transact(sock, client_endpoint, pi_address,
                        build_setting_request(request_id, SET_SAMPLE_RATE_COMMAND, 0, 2000000), timeout)
    sample_rate_hz = validate_setting(response, request_id, SET_SAMPLE_RATE_COMMAND, 0, 2000000)
    request_id = next_request_id(request_id)
    response = transact(sock, client_endpoint, pi_address,
                        build_setting_request(request_id, SET_BANDWIDTH_COMMAND, 0, 1750000), timeout)
    bandwidth_hz = validate_setting(response, request_id, SET_BANDWIDTH_COMMAND, 0, 1750000)

    total_points = (stop_khz - start_khz) // step_khz + 1
    requested_frequencies = [start_khz + index * step_khz for index in range(total_points)]
    measured = {}
    for offset in range(0, total_points, 128):
        chunk = requested_frequencies[offset:offset + 128]
        request_id = next_request_id(request_id)
        packet = build_sweep_request(request_id, 0, chunk[0], chunk[-1], step_khz)
        response = transact(sock, client_endpoint, pi_address, packet, timeout)
        points = parse_sweep_response(response, request_id, 0, len(chunk))
        for requested_khz, (actual_khz, power_cdbfs) in zip(chunk, points):
            measured[requested_khz] = (actual_khz, power_cdbfs)
        print("{}: {}/{} points captured".format(
            frontend_name, len(measured), total_points), flush=True)

    return request_id, measured, sample_rate_hz, bandwidth_hz, total_gain_cdb


def write_csv(path, start_khz, stop_khz, grid_step_khz, measured, sweep_step_khz, frontend_gain_cdb):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("frequency_mhz", "power_dbfs", "actual_frequency_mhz",
                 "frontend_gain_db", "status"))
        for frequency_khz in range(start_khz, stop_khz + 1, grid_step_khz):
            result = measured.get(frequency_khz)
            if result is None:
                writer.writerow(("{:.3f}".format(frequency_khz / 1000), "", "", "", "not_measured"))
            else:
                actual_khz, power_cdbfs = result
                writer.writerow(("{:.3f}".format(frequency_khz / 1000),
                                 "{:.2f}".format(power_cdbfs / 100),
                                 "{:.3f}".format(actual_khz / 1000),
                                 "{:.2f}".format(frontend_gain_cdb / 100), "measured"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-address", required=True, help="Windows Wi-Fi IPv4 address")
    parser.add_argument("--pi-address", default="10.123.71.141")
    parser.add_argument("--pi-user", default="rpi")
    parser.add_argument("--key-file", default=str(pathlib.Path.home() / ".ssh" / "rpi_scanner_ed25519"))
    parser.add_argument("--binary", default="/mnt/scaner-ram/scanner-node-validation/scanner_node/scanner_node")
    parser.add_argument("--device-json", default="/mnt/scaner-ram/scanner-node-validation/device.json")
    parser.add_argument("--port", type=int, default=2653)
    parser.add_argument("--start-mhz", type=int, default=100)
    parser.add_argument("--stop-mhz", type=int, default=6000)
    parser.add_argument("--step-mhz", type=int, default=20)
    parser.add_argument("--grid-step-mhz", type=int, default=10)
    parser.add_argument("--hackrf-lna-gain-db", type=int, default=16)
    parser.add_argument("--hackrf-vga-gain-db", type=int, default=20)
    parser.add_argument("--output-dir", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / "measurements")
    parser.add_argument("--frontend-ids", default="0,1",
                        help="comma-separated Pi discovery IDs (verified: 0=BladeRF, 1=HackRF)")
    parser.add_argument("--timeout", type=int, default=300,
                        help="seconds allowed for each sweep response")
    args = parser.parse_args()
    if (args.start_mhz < 70 or args.stop_mhz > 6000 or args.start_mhz >= args.stop_mhz
            or args.step_mhz <= 0 or args.grid_step_mhz <= 0
            or args.step_mhz % args.grid_step_mhz != 0):
        parser.error("require 70 <= start < stop <= 6000 MHz and positive aligned steps")
    if not 0 <= args.hackrf_lna_gain_db <= 40 or args.hackrf_lna_gain_db % 8 != 0:
        parser.error("HackRF LNA gain must be 0..40 dB in 8 dB steps")
    if not 0 <= args.hackrf_vga_gain_db <= 62 or args.hackrf_vga_gain_db % 2 != 0:
        parser.error("HackRF VGA gain must be 0..62 dB in 2 dB steps")

    ids = [int(value) for value in args.frontend_ids.split(",")]
    ssh_command = ["ssh", "-i", args.key_file, "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes",
                   "-o", "ConnectTimeout=10", "{}@{}".format(args.pi_user, args.pi_address),
                   "{} --server {} --server-port {} --local-port 3333 --device-json {} "
                   "--software-version 1.0.0.0 --attempts 1 --handshake-timeout 10000 "
                   "--ver-timeout 10000".format(shlex.quote(args.binary), args.windows_address,
                                                  args.port, shlex.quote(args.device_json))]

    timestamp = datetime.datetime.now().strftime("%Y%m%dT%H%M%S")
    scanner_process = None
    completed = False
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind((args.windows_address, args.port))
        scanner_process = subprocess.Popen(ssh_command, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, text=True)
        try:
            client_endpoint, inventory = connect_scanner(
                server, args.pi_address, args.pi_user, args.key_file, args.windows_address,
                args.port, args.binary, args.device_json, 30
            )
            print("Scanner connected from {}:{}; {} frontend(s) found".format(
                *client_endpoint, len(inventory["frontends"])), flush=True)
            profiles = {item["id"]: item for item in inventory["frontends"]}
            names = {}
            for frontend_id, profile in profiles.items():
                if profile["tx_channels"] == 0:
                    names[frontend_id] = "stub_sdr"
                elif profile["sample_resolution_bits"] == 8 and profile["rx_channels"] == 1:
                    names[frontend_id] = "hackrf_one"
                elif profile["rx_channels"] >= 2:
                    names[frontend_id] = "bladerf2_micro"
                else:
                    names[frontend_id] = "frontend_{}".format(frontend_id)
            if len(profiles) == 1 and names[next(iter(profiles))] == "stub_sdr" and ids == [0, 1]:
                ids = [next(iter(profiles))]
            frontend_ids_in_inventory = {item["id"] for item in inventory["frontends"]}
            if not set(ids).issubset(frontend_ids_in_inventory):
                raise RuntimeError("requested frontend IDs not present: {}".format(ids))

            start_khz = args.start_mhz * 1000
            stop_khz = args.stop_mhz * 1000
            step_khz = args.step_mhz * 1000
            grid_step_khz = args.grid_step_mhz * 1000
            results = {}
            request_id = 2
            for frontend_id in ids:
                request_id, measurements, sample_rate_hz, bandwidth_hz, total_gain_cdb = scan_one_frontend(
                    server, client_endpoint, args.pi_address, frontend_id,
                    names[frontend_id],
                    start_khz, stop_khz, step_khz, args.timeout,
                    args.hackrf_lna_gain_db, args.hackrf_vga_gain_db
                )
                results[frontend_id] = measurements
                frontend_name = names[frontend_id]
                if frontend_name == "hackrf_one":
                    gain_tag = "hackrfLNA{}VGA{}".format(
                        args.hackrf_lna_gain_db, args.hackrf_vga_gain_db
                    )
                elif frontend_name == "stub_sdr":
                    gain_tag = "synthetic"
                else:
                    gain_tag = "totalGain{}dB".format(total_gain_cdb / 100)
                filename = "air_sweep_{}_{}-{}MHz_sample{}MHz_grid{}MHz_{}_{}.csv".format(
                    frontend_name, args.start_mhz, args.stop_mhz, args.step_mhz,
                    args.grid_step_mhz, gain_tag, timestamp
                )
                output_path = args.output_dir / filename
                write_csv(output_path, start_khz, stop_khz, grid_step_khz,
                          measurements, step_khz, total_gain_cdb)
                print("Saved {} measured points to {} (sample rate {} Hz, bandwidth {} Hz, frontend total gain {:.2f} dB)".format(
                    len(measurements), output_path, sample_rate_hz, bandwidth_hz,
                    total_gain_cdb / 100), flush=True)

            exit_id = next_request_id(request_id)
            exit_response = transact(server, client_endpoint, args.pi_address,
                                     build_exit_request(exit_id), 15)
            validate_exit_response(exit_response, exit_id)
            completed = True
        finally:
            if not completed and scanner_process.poll() is None:
                try:
                    exit_id = next_request_id(locals().get("request_id", 2))
                    server.sendto(build_exit_request(exit_id), client_endpoint)
                    receive_from_pi(server, args.pi_address, 5)
                except Exception:
                    scanner_process.terminate()
            try:
                stdout, _ = scanner_process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                scanner_process.terminate()
                stdout, _ = scanner_process.communicate(timeout=5)
            if stdout:
                print(stdout, end="", flush=True)
            if completed and scanner_process.returncode != 0:
                raise RuntimeError("scanner SSH process exited with status {}".format(
                    scanner_process.returncode))


if __name__ == "__main__":
    main()