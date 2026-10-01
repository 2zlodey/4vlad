#!/usr/bin/env python3
"""Small UDP mock for the Orkestr.DemoServer handshake and VER exchange."""

import argparse
import secrets
import socket
import struct
import sys
import time


HANDSHAKE_SIZE = 626
SESSION_REPLY_SIZE = 52
VER_REQUEST_SIZE = 5
VER_RESPONSE_SIZE = 14
VER_COMMAND = 0x01
RADIO_FRONTENDS_COMMAND = 0x04
SET_ACTIVE_RADIO_COMMAND = 0x05
RADIO_CAPABILITY_SIZE = 128
RADIO_MAX_BANDWIDTH_OPTIONS = 16
RADIO_STATUS_OK = 0


class MockProtocolError(Exception):
    """Raised when a received packet does not match the DemoServer wire format."""


def parse_handshake(payload):
    if len(payload) < HANDSHAKE_SIZE:
        raise MockProtocolError(
            "short handshake: got {} bytes, expected at least {}".format(
                len(payload), HANDSHAKE_SIZE
            )
        )
    counter, seconds, microseconds, reply_port = struct.unpack_from("<IqqH", payload)
    device_id = struct.unpack_from("<i", payload, 22)[0]
    if reply_port == 0:
        raise MockProtocolError("handshake reply port is zero")
    return {
        "counter": counter,
        "seconds": seconds,
        "microseconds": microseconds,
        "reply_port": reply_port,
        "device_id": device_id,
    }


def build_session_reply():
    key = secrets.token_bytes(32)
    nonce = secrets.token_bytes(12)
    timestamp_ms = time.time_ns() // 1_000_000
    return key + nonce + struct.pack("<q", timestamp_ms)


def build_ver_request(request_id):
    return struct.pack("<IB", request_id, VER_COMMAND)


def build_ver_response(request_id, version):
    try:
        version_bytes = version.encode("ascii")
    except UnicodeEncodeError as error:
        raise MockProtocolError("software version must be ASCII") from error
    if not version_bytes or len(version_bytes) > 10:
        raise MockProtocolError("software version must contain 1 to 10 ASCII bytes")
    return struct.pack("<I", request_id) + version_bytes.ljust(10, b"\0")


def validate_ver_response(payload, expected_id, expected_version):
    if len(payload) != VER_RESPONSE_SIZE:
        raise MockProtocolError(
            "VER response has {} bytes, expected {}".format(
                len(payload), VER_RESPONSE_SIZE
            )
        )
    actual_id = struct.unpack_from("<I", payload)[0]
    if actual_id != expected_id:
        raise MockProtocolError(
            "VER RequestId mismatch: got {}, expected {}".format(actual_id, expected_id)
        )
    expected_version_bytes = build_ver_response(expected_id, expected_version)[4:]
    if payload[4:] != expected_version_bytes:
        raise MockProtocolError(
            "VER version mismatch: got {!r}, expected {!r}".format(
                payload[4:], expected_version_bytes
            )
        )


def build_radio_frontends_request(request_id):
    return struct.pack("<IB", request_id, RADIO_FRONTENDS_COMMAND)


def parse_radio_frontends_response(payload, expected_id):
    if len(payload) < 8:
        raise MockProtocolError("radio frontend response is shorter than 8 bytes")
    request_id, command, status, count, active_id = struct.unpack_from("<IBBBB", payload)
    if request_id != expected_id or command != RADIO_FRONTENDS_COMMAND:
        raise MockProtocolError("radio frontend response request id or command mismatch")
    if status != RADIO_STATUS_OK:
        raise MockProtocolError("radio frontend query returned status {}".format(status))
    if len(payload) != 8 + count * RADIO_CAPABILITY_SIZE:
        raise MockProtocolError("radio frontend response size does not match its count")

    frontends = []
    for index in range(count):
        values = struct.unpack_from("<BBBBIQQIIIIIIIhhhBBBBBB16I", payload, 8 + index * RADIO_CAPABILITY_SIZE)
        frontend = {
            "id": values[0],
            "rx_channels": values[1],
            "tx_channels": values[2],
            "flags": values[3],
            "capabilities": values[4],
            "frequency_min_hz": values[5],
            "frequency_max_hz": values[6],
            "frequency_step_hz": values[7],
            "sample_rate_min_hz": values[8],
            "sample_rate_max_hz": values[9],
            "sample_rate_step_hz": values[10],
            "bandwidth_min_hz": values[11],
            "bandwidth_max_hz": values[12],
            "bandwidth_step_hz": values[13],
            "gain_min_cdb": values[14],
            "gain_max_cdb": values[15],
            "gain_step_cdb": values[16],
            "sample_resolution_bits": values[17],
            "iq_sample_format": values[18],
            "agc_modes": values[19],
            "bandwidth_option_count": values[20],
            "antenna_paths": values[21],
            "bandwidth_options": list(values[23 : 23 + values[20]]),
        }
        if frontend["bandwidth_option_count"] > RADIO_MAX_BANDWIDTH_OPTIONS:
            raise MockProtocolError("too many bandwidth options")
        if frontend["frequency_min_hz"] > frontend["frequency_max_hz"]:
            raise MockProtocolError("invalid frontend frequency range")
        if frontend["sample_rate_min_hz"] > frontend["sample_rate_max_hz"]:
            raise MockProtocolError("invalid frontend sample-rate range")
        if frontend["bandwidth_min_hz"] > frontend["bandwidth_max_hz"]:
            raise MockProtocolError("invalid frontend bandwidth range")
        options = frontend["bandwidth_options"]
        if options and (options != sorted(set(options))
                        or options[0] < frontend["bandwidth_min_hz"]
                        or options[-1] > frontend["bandwidth_max_hz"]):
            raise MockProtocolError("invalid discrete bandwidth options")
        frontends.append(frontend)
    if len({frontend["id"] for frontend in frontends}) != count:
        raise MockProtocolError("duplicate frontend id")
    return {"active_id": active_id, "frontends": frontends}


def build_set_active_radio_request(request_id, frontend_id):
    return struct.pack("<IBB", request_id, SET_ACTIVE_RADIO_COMMAND, frontend_id)


def validate_set_active_radio_response(payload, expected_id, expected_frontend_id):
    if len(payload) != 7:
        raise MockProtocolError("active radio response has {} bytes, expected 7".format(len(payload)))
    request_id, command, status, active_id = struct.unpack("<IBBB", payload)
    if request_id != expected_id or command != SET_ACTIVE_RADIO_COMMAND:
        raise MockProtocolError("active radio response request id or command mismatch")
    if status != RADIO_STATUS_OK or active_id != expected_frontend_id:
        raise MockProtocolError("active radio selection failed or returned a different id")


def build_radio_frontends_response(request_id):
    bandwidth_options = [1750000, 2500000] + [0] * (RADIO_MAX_BANDWIDTH_OPTIONS - 2)
    record = struct.pack(
        "<BBBBIQQIIIIIIIhhhBBBBBB16I",
        0,
        2,
        2,
        1,
        0x3F,
        70000000,
        6000000000,
        1,
        2000000,
        20000000,
        0,
        1750000,
        28000000,
        0,
        -1500,
        6000,
        100,
        12,
        2,
        1,
        2,
        2,
        0,
        *bandwidth_options,
    )
    return struct.pack("<IBBBB", request_id, RADIO_FRONTENDS_COMMAND, RADIO_STATUS_OK, 1, 0xFF) + record


def build_set_active_radio_response(request_id, frontend_id):
    return struct.pack("<IBBB", request_id, SET_ACTIVE_RADIO_COMMAND, RADIO_STATUS_OK, frontend_id)


def serve_one(sock, version, timeout, request_id, radio_commands=False, frontend_id=0):
    sock.settimeout(timeout)
    payload, source = sock.recvfrom(65535)
    handshake = parse_handshake(payload)
    client_endpoint = (source[0], handshake["reply_port"])

    sock.sendto(build_session_reply(), client_endpoint)
    sock.sendto(build_ver_request(request_id), client_endpoint)

    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("timed out waiting for VER response")
        sock.settimeout(remaining)
        response, response_source = sock.recvfrom(65535)
        if response_source != source:
            print(
                "Ignoring VER response from unexpected endpoint {}:{}".format(*response_source),
                file=sys.stderr,
            )
            continue
        validate_ver_response(response, request_id, version)
        radio_result = None
        if radio_commands:
            query_id = 1 if request_id == 0xFFFFFFFF else request_id + 1
            sock.sendto(build_radio_frontends_request(query_id), client_endpoint)
            frontend_response, frontend_source = sock.recvfrom(65535)
            if frontend_source != source:
                raise MockProtocolError("radio frontend response came from an unexpected endpoint")
            radio_result = parse_radio_frontends_response(frontend_response, query_id)
            if not radio_result["frontends"]:
                raise MockProtocolError("no radio frontends found")
            if frontend_id not in {item["id"] for item in radio_result["frontends"]}:
                raise MockProtocolError("requested active frontend id is not available")

            select_id = 1 if query_id == 0xFFFFFFFF else query_id + 1
            sock.sendto(build_set_active_radio_request(select_id, frontend_id), client_endpoint)
            select_response, select_source = sock.recvfrom(65535)
            if select_source != source:
                raise MockProtocolError("active radio response came from an unexpected endpoint")
            validate_set_active_radio_response(select_response, select_id, frontend_id)
        return handshake, source, radio_result


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bind-address",
        default="127.0.0.1",
        help="IPv4 bind address; use 0.0.0.0 to accept LAN clients (default: loopback)",
    )
    parser.add_argument("--port", type=int, default=2653, help="UDP port (default: 2653)")
    parser.add_argument(
        "--version", default="1.0.0.0", help="10-byte-or-shorter ASCII VER value"
    )
    parser.add_argument(
        "--timeout", type=float, default=10.0, help="per phase timeout in seconds (default: 10)"
    )
    parser.add_argument(
        "--clients",
        type=int,
        default=1,
        help="number of successful clients; 0 serves indefinitely (default: 1)",
    )
    parser.add_argument(
        "--radio-commands",
        action="store_true",
        help="query generic radio capabilities and select a frontend after VER",
    )
    parser.add_argument(
        "--select-frontend-id",
        type=int,
        default=0,
        help="logical frontend id to activate with --radio-commands (default: 0)",
    )
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.clients < 0:
        parser.error("--clients cannot be negative")
    if not 0 <= args.select_frontend_id <= 255:
        parser.error("--select-frontend-id must be between 0 and 255")
    try:
        build_ver_response(1, args.version)
    except MockProtocolError as error:
        parser.error(str(error))
    try:
        socket.inet_pton(socket.AF_INET, args.bind_address)
    except OSError:
        parser.error("--bind-address must be an IPv4 address")
    return args


def main(argv=None):
    args = parse_args(argv)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
            udp_socket.bind((args.bind_address, args.port))
            print(
                "DemoServer mock listening on {}:{}; VER version={!r}".format(
                    args.bind_address, args.port, args.version
                ),
                flush=True,
            )
            request_id = 1
            successful_clients = 0
            while args.clients == 0 or successful_clients < args.clients:
                try:
                    handshake, source, radio_result = serve_one(
                        udp_socket,
                        args.version,
                        args.timeout,
                        request_id,
                        args.radio_commands,
                        args.select_frontend_id,
                    )
                except socket.timeout:
                    if args.clients == 0:
                        continue
                    raise TimeoutError("timed out waiting for handshake")
                successful_clients += 1
                print(
                    "OK device_id={} counter={} client={}:{} reply_port={} VER id={} version={!r}".format(
                        handshake["device_id"],
                        handshake["counter"],
                        source[0],
                        source[1],
                        handshake["reply_port"],
                        request_id,
                        args.version,
                    ),
                    flush=True,
                )
                if radio_result is not None:
                    print(
                        "RADIO frontends={} selected={}".format(
                            len(radio_result["frontends"]), args.select_frontend_id
                        ),
                        flush=True,
                    )
                request_id = 1 if request_id == 0xFFFFFFFF else request_id + 1
        return 0
    except (OSError, MockProtocolError, TimeoutError) as error:
        print("DemoServer mock: {}".format(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("DemoServer mock stopped", flush=True)
        return 0


if __name__ == "__main__":
    sys.exit(main())