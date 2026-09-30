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


def serve_one(sock, version, timeout, request_id):
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
        return handshake, source


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
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.clients < 0:
        parser.error("--clients cannot be negative")
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
                    handshake, source = serve_one(
                        udp_socket, args.version, args.timeout, request_id
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