#!/usr/bin/env python3
"""Run one detector API scan against a remote scanner_node over UDP."""

import argparse
import os
import posixpath
import shlex
import socket
import struct
import subprocess
import tempfile
import time

from demo_server_mock import (
    DETECT_ALGORITHM_CA_CFAR,
    DETECT_GET_RESULT_COMMAND,
    DETECT_PAGE_SIZE,
    DETECT_START_COMMAND,
    RADIO_STATUS_OK,
    build_session_reply,
    build_ver_request,
    parse_radio_frontends_response,
)


def receive(sock, expected_address, timeout, description):
    sock.settimeout(timeout)
    payload, source = sock.recvfrom(65535)
    if source[0] != expected_address:
        raise RuntimeError("{} came from unexpected source {}".format(description, source))
    return payload


def exchange(sock, endpoint, expected_address, request, request_id, opcode, timeout=10.0):
    sock.sendto(request, endpoint)
    response = receive(sock, expected_address, timeout, "response to opcode 0x{:02x}".format(opcode))
    if len(response) < 6:
        raise RuntimeError("short response to opcode 0x{:02x}: {} bytes".format(opcode, len(response)))
    response_id, response_opcode = struct.unpack_from("<IB", response)
    if (response_id, response_opcode) != (request_id, opcode):
        raise RuntimeError("response ID/opcode mismatch: {} / 0x{:02x}".format(response_id, response_opcode))
    return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pi-address", default="10.13.16.141")
    parser.add_argument("--ssh-user", default="rpi")
    parser.add_argument("--ssh-key", default=os.path.expanduser("~/.ssh/rpi_scanner_ed25519"))
    parser.add_argument("--run-root", default="/mnt/scaner-ram/scanner-refactor/run_20261119_212606")
    parser.add_argument("--bind-address", default="10.13.16.169")
    parser.add_argument("--server-port", type=int, default=26531)
    parser.add_argument("--radio-port", type=int, default=3333)
    parser.add_argument("--channel", type=int, default=1)
    parser.add_argument("--antenna-id", type=int, default=0)
    parser.add_argument("--start-khz", type=int, default=80000)
    parser.add_argument("--stop-khz", type=int, default=300000)
    parser.add_argument("--dwell-ms", type=int, default=100)
    parser.add_argument("--fft-size", type=int, default=2048)
    parser.add_argument("--sensitivity-db", type=float, default=6.0)
    parser.add_argument("--pfa-ppm", type=int, default=1000)
    parser.add_argument("--training-bins", type=int, default=16)
    parser.add_argument("--guard-bins", type=int, default=4)
    parser.add_argument("--poll-ms", type=int, default=100)
    parser.add_argument("--result-timeout-sec", type=float, default=90.0,
                        help="maximum wait for one GET_RESULT response")
    args = parser.parse_args()

    if args.stop_khz <= args.start_khz or args.channel not in (0, 1):
        parser.error("Require stop > start and RX channel 0 or 1")
    if not os.path.isfile(args.ssh_key):
        parser.error("SSH key not found: {}".format(args.ssh_key))

    scanner = posixpath.join(args.run_root, "scanner_node", "build-pi", "scanner_node")
    device_json = posixpath.join(args.run_root, "device.json")
    remote_command = "cd {} && {} --server {} --server-port {} --bind-address 0.0.0.0 --local-port {} --reply-port {} --device-json {} --debug 1".format(
        shlex.quote(posixpath.dirname(scanner)),
        shlex.quote(scanner),
        shlex.quote(args.bind_address),
        args.server_port,
        args.radio_port,
        args.radio_port,
        shlex.quote(device_json),
    )
    ssh_command = [
        "ssh",
        "-T",
        "-i",
        args.ssh_key,
        "-o",
        "IdentitiesOnly=yes",
        "-o",
        "BatchMode=yes",
        "-o",
        "ConnectTimeout=10",
        "-o",
        "ServerAliveInterval=5",
        "-o",
        "ServerAliveCountMax=2",
        "{}@{}".format(args.ssh_user, args.pi_address),
        remote_command,
    ]

    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind((args.bind_address, args.server_port))
    server.settimeout(0.5)
    process = None
    log_file = tempfile.NamedTemporaryFile(prefix="blade-rf-scan-", suffix=".log", delete=False)
    log_path = log_file.name
    session_ready = False
    exited = False
    request_id = 1
    try:
        process = subprocess.Popen(ssh_command, stdin=subprocess.DEVNULL, stdout=log_file, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15.0
        handshake = None
        source = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("scanner SSH process exited before handshake; see {}".format(log_path))
            try:
                handshake, source = server.recvfrom(65535)
                break
            except socket.timeout:
                continue
        if handshake is None or len(handshake) != 626:
            raise RuntimeError("did not receive the 626-byte scanner handshake")
        handshake_id, seconds, micros, radio_port = struct.unpack_from("<IqqH", handshake)
        endpoint = (source[0], radio_port)
        server.sendto(build_session_reply(), endpoint)

        server.sendto(build_ver_request(request_id), endpoint)
        ver_response = receive(server, args.pi_address, 8.0, "VER response")
        if len(ver_response) != 14 or struct.unpack_from("<I", ver_response)[0] != request_id:
            raise RuntimeError("invalid VER response")
        print("Session established with {} device_id={}".format(args.pi_address, struct.unpack_from("<i", handshake, 22)[0]))
        session_ready = True

        request_id += 1
        frontend_request = struct.pack("<IB", request_id, 0x04)
        frontend_response = exchange(server, endpoint, args.pi_address, frontend_request, request_id, 0x04)
        inventory = parse_radio_frontends_response(frontend_response, request_id)
        compatible = [item for item in inventory["frontends"]
                      if item["rx_channels"] > args.channel and item["capabilities"] & 0x01]
        blade = next((item for item in compatible if item["sample_resolution_bits"] == 12), None)
        if blade is None:
            raise RuntimeError("no 12-bit RX frontend exposes channel {}".format(args.channel))
        print("BladeRF candidate frontend={} RX={} frequency={}..{} Hz sample-rate={}..{} Hz BW={}..{} Hz".format(
            blade["id"], blade["rx_channels"], blade["frequency_min_hz"], blade["frequency_max_hz"],
            blade["sample_rate_min_hz"], blade["sample_rate_max_hz"],
            blade["bandwidth_min_hz"], blade["bandwidth_max_hz"]))

        request_id += 1
        select_request = struct.pack("<IBB", request_id, 0x05, blade["id"])
        select_response = exchange(server, endpoint, args.pi_address, select_request, request_id, 0x05)
        if len(select_response) != 7 or select_response[5] != RADIO_STATUS_OK:
            raise RuntimeError("could not select BladeRF frontend {}".format(blade["id"]))

        request_id += 1
        start = struct.pack(
            "<IBBBBIIIHhIBB",
            request_id,
            DETECT_START_COMMAND,
            args.channel,
            args.antenna_id,
            DETECT_ALGORITHM_CA_CFAR,
            args.start_khz,
            args.stop_khz,
            args.dwell_ms,
            args.fft_size,
            int(round(args.sensitivity_db * 100)),
            args.pfa_ppm,
            args.training_bins,
            args.guard_bins,
        )
        start_response = exchange(server, endpoint, args.pi_address, start, request_id, DETECT_START_COMMAND)
        if len(start_response) != 11 or start_response[5] != RADIO_STATUS_OK:
            raise RuntimeError("DETECT_START failed: status {}".format(start_response[5] if len(start_response) > 5 else "short"))
        job_id = struct.unpack_from("<I", start_response, 6)[0]
        effective_antenna = start_response[10]
        print("Started job={} channel={} antenna={} algorithm=1 band={}..{} kHz dwell={} ms FFT={} SNR>={} dB".format(
            job_id, args.channel, effective_antenna, args.start_khz, args.stop_khz,
            args.dwell_ms, args.fft_size, args.sensitivity_db))

        total_candidates = 0
        state = 0
        candidate_pages = []
        last_progress_bucket = -1
        while state != 2:
            time.sleep(args.poll_ms / 1000.0)
            request_id += 1
            poll = struct.pack("<IBII", request_id, DETECT_GET_RESULT_COMMAND, job_id, 0)
            response = exchange(server, endpoint, args.pi_address, poll, request_id,
                                DETECT_GET_RESULT_COMMAND, args.result_timeout_sec)
            if len(response) < 25:
                raise RuntimeError("short detector result header")
            status = response[5]
            state = response[10]
            progress = struct.unpack_from("<H", response, 11)[0]
            if status != RADIO_STATUS_OK:
                raise RuntimeError("detector job failed status={} state={}".format(status, state))
            total_candidates = struct.unpack_from("<I", response, 15)[0]
            progress_bucket = progress // 250
            if progress_bucket != last_progress_bucket or state == 2:
                print("progress={}‰ candidates={}".format(progress, total_candidates), flush=True)
                last_progress_bucket = progress_bucket
            if state == 3:
                raise RuntimeError("detector job failed")

        offset = 0
        while offset < total_candidates:
            request_id += 1
            page_request = struct.pack("<IBII", request_id, DETECT_GET_RESULT_COMMAND, job_id, offset)
            response = exchange(server, endpoint, args.pi_address, page_request, request_id,
                                DETECT_GET_RESULT_COMMAND)
            count = struct.unpack_from("<H", response, 23)[0]
            if count == 0:
                raise RuntimeError("empty candidate page at offset {} of {}".format(offset, total_candidates))
            for index in range(count):
                center_hz, bandwidth_hz, power_cdbfs, snr_cdb = struct.unpack_from(
                    "<QIhh", response, 25 + index * 16
                )
                candidate_pages.append((center_hz, bandwidth_hz, power_cdbfs / 100.0, snr_cdb / 100.0))
            offset += count

        print("\nCandidates (center MHz | width kHz | power dBFS | SNR dB)")
        for center_hz, bandwidth_hz, power_dbfs, snr_db in sorted(candidate_pages):
            print("{:10.6f} | {:9.2f} | {:8.2f} | {:6.2f}".format(
                center_hz / 1e6, bandwidth_hz / 1e3, power_dbfs, snr_db
            ))
        if not candidate_pages:
            print("No candidates exceeded the configured sensitivity.")
    finally:
        if session_ready and process is not None and process.poll() is None:
            try:
                request_id += 1
                server.sendto(struct.pack("<IB", request_id, 0xDE), endpoint)
                response = receive(server, args.pi_address, 3.0, "EXIT response")
                exited = len(response) >= 6 and response[4] == 0xDE
            except (OSError, RuntimeError):
                pass
        server.close()
        log_file.close()
        if process is not None:
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
            print("Scanner exited={}; SSH return code={}; log={}".format(exited, process.returncode, log_path))


if __name__ == "__main__":
    main()