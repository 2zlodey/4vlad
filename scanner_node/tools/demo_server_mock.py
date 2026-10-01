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
EXIT_COMMAND = 0x06
SET_FREQUENCY_COMMAND = 0x64
GET_FREQUENCY_COMMAND = 0x6A
SET_SAMPLE_RATE_COMMAND = 0x65
SET_LNA_GAIN_COMMAND = 0x66
SET_VGA_GAIN_COMMAND = 0x67
SET_BANDWIDTH_COMMAND = 0x68
GET_GAIN_COMMAND = 0x6B
GET_SAMPLE_RATE_COMMAND = 0x6C
MEASURE_CURRENT_COMMAND = 0x6D
MEASURE_FREQUENCY_COMMAND = 0x02
SWEEP_COMMAND = 0x03
GET_RAW_IQ_COMMAND = 0x70
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


def build_set_frequency_request(request_id, channel, frequency_khz):
    return struct.pack("<IBBI", request_id, SET_FREQUENCY_COMMAND, channel, frequency_khz)


def build_get_frequency_request(request_id, channel):
    return struct.pack("<IBB", request_id, GET_FREQUENCY_COMMAND, channel)


def build_frequency_response(request_id, command, status, channel, frequency_hz):
    return struct.pack("<IBBBQ", request_id, command, status, channel, frequency_hz)


def validate_frequency_response(payload, expected_id, expected_command, expected_channel, expected_frequency_hz,
                                expected_status=RADIO_STATUS_OK):
    if len(payload) != 15:
        raise MockProtocolError("frequency response has {} bytes, expected 15".format(len(payload)))
    request_id, command, status, channel, frequency_hz = struct.unpack("<IBBBQ", payload)
    if request_id != expected_id or command != expected_command:
        raise MockProtocolError("frequency response request id or command mismatch")
    if status != expected_status or channel != expected_channel or frequency_hz != expected_frequency_hz:
        raise MockProtocolError("frequency response status, channel, or readback mismatch")


def build_set_u32_request(request_id, command, channel, value):
    return struct.pack("<IBBI", request_id, command, channel, value)


def build_get_value_request(request_id, command, channel):
    return struct.pack("<IBB", request_id, command, channel)


def build_set_gain_request(request_id, command, channel, gain_db):
    return struct.pack("<IBBB", request_id, command, channel, gain_db)


def build_measure_frequency_request(request_id, channel, frequency_khz):
    return struct.pack("<IBBI", request_id, MEASURE_FREQUENCY_COMMAND, channel, frequency_khz)


def build_sweep_request(request_id, channel, start_khz, stop_khz, step_khz):
    return struct.pack("<IBBIII", request_id, SWEEP_COMMAND, channel, start_khz, stop_khz, step_khz)


def build_raw_iq_request(request_id, channel, complex_pairs):
    return struct.pack("<IBBH", request_id, GET_RAW_IQ_COMMAND, channel, complex_pairs)


def validate_setting_response(payload, expected_id, command, channel, value):
    if len(payload) != 11:
        raise MockProtocolError("setting response has {} bytes, expected 11".format(len(payload)))
    actual_id, actual_command, status, actual_channel, actual_value = struct.unpack("<IBBBI", payload)
    if (actual_id, actual_command, status, actual_channel, actual_value) != (
        expected_id, command, RADIO_STATUS_OK, channel, value
    ):
        raise MockProtocolError("setting response id, command, status, channel, or readback mismatch")


def validate_gain_stage_response(payload, expected_id, command, channel, gain_db):
    if len(payload) != 8:
        raise MockProtocolError("gain-stage response has {} bytes, expected 8".format(len(payload)))
    actual_id, actual_command, status, actual_channel, actual_gain = struct.unpack("<IBBBB", payload)
    if (actual_id, actual_command, status, actual_channel, actual_gain) != (
        expected_id, command, RADIO_STATUS_OK, channel, gain_db
    ):
        raise MockProtocolError("gain-stage response id, command, status, channel, or readback mismatch")


def validate_signed_value_response(payload, expected_id, command, channel):
    if len(payload) != 9:
        raise MockProtocolError("signed-value response has {} bytes, expected 9".format(len(payload)))
    actual_id, actual_command, status, actual_channel, value = struct.unpack("<IBBBh", payload)
    if actual_id != expected_id or actual_command != command or status != RADIO_STATUS_OK or actual_channel != channel:
        raise MockProtocolError("signed-value response header mismatch")
    return value


def validate_raw_iq_response(payload, expected_id, channel, expected_pairs, expected_format):
    if len(payload) < 10:
        raise MockProtocolError("raw-IQ response is shorter than 10 bytes")
    actual_id, command, status, actual_channel, sample_format, pairs = struct.unpack_from("<IBBBBH", payload)
    bytes_per_pair = 2 if sample_format == 1 else 4 if sample_format == 2 else 0
    if (actual_id, command, status, actual_channel, sample_format, pairs) != (
        expected_id, GET_RAW_IQ_COMMAND, RADIO_STATUS_OK, channel, expected_format, expected_pairs
    ):
        raise MockProtocolError("raw-IQ response header mismatch")
    if len(payload) != 10 + pairs * bytes_per_pair:
        raise MockProtocolError("raw-IQ payload length does not match format and pair count")
    return payload[10:]


def validate_sweep_response(payload, expected_id, channel, expected_frequencies):
    if len(payload) < 9:
        raise MockProtocolError("sweep response is shorter than 9 bytes")
    actual_id, command, status, actual_channel, count = struct.unpack_from("<IBBBH", payload)
    if (actual_id, command, status, actual_channel, count) != (
        expected_id, SWEEP_COMMAND, RADIO_STATUS_OK, channel, len(expected_frequencies)
    ):
        raise MockProtocolError("sweep response header mismatch")
    if len(payload) != 9 + count * 6:
        raise MockProtocolError("sweep response size does not match point count")
    points = [struct.unpack_from("<Ih", payload, 9 + index * 6) for index in range(count)]
    if [point[0] for point in points] != expected_frequencies:
        raise MockProtocolError("sweep frequencies mismatch")
    return [point[1] for point in points]


def exchange_radio_request(sock, endpoint, request):
    sock.sendto(request, endpoint)
    response, source = sock.recvfrom(65535)
    if source != endpoint:
        raise MockProtocolError("radio response came from an unexpected endpoint")
    return response


def exercise_radio_commands(sock, endpoint, request_id, frontend, frequency_khz):
    sample_rate_hz = min(max(frontend["sample_rate_min_hz"], 2000000),
                         frontend["sample_rate_max_hz"])
    sample_rate_id = next_request_id(request_id)
    response = exchange_radio_request(
        sock, endpoint,
        build_set_u32_request(sample_rate_id, SET_SAMPLE_RATE_COMMAND, 0, sample_rate_hz),
    )
    validate_setting_response(response, sample_rate_id, SET_SAMPLE_RATE_COMMAND, 0, sample_rate_hz)
    get_sample_rate_id = next_request_id(sample_rate_id)
    response = exchange_radio_request(
        sock, endpoint, build_get_value_request(get_sample_rate_id, GET_SAMPLE_RATE_COMMAND, 0)
    )
    validate_setting_response(response, get_sample_rate_id, GET_SAMPLE_RATE_COMMAND, 0, sample_rate_hz)

    bandwidth_hz = frontend["bandwidth_options"][0] if frontend["bandwidth_options"] else max(
        frontend["bandwidth_min_hz"], 1000000
    )
    bandwidth_id = next_request_id(get_sample_rate_id)
    response = exchange_radio_request(
        sock, endpoint,
        build_set_u32_request(bandwidth_id, SET_BANDWIDTH_COMMAND, 0, bandwidth_hz),
    )
    validate_setting_response(response, bandwidth_id, SET_BANDWIDTH_COMMAND, 0, bandwidth_hz)

    lna_gain_db, vga_gain_db = (16, 20) if frontend["sample_resolution_bits"] == 8 else (6, 10)
    lna_id = next_request_id(bandwidth_id)
    response = exchange_radio_request(
        sock, endpoint, build_set_gain_request(lna_id, SET_LNA_GAIN_COMMAND, 0, lna_gain_db)
    )
    validate_gain_stage_response(response, lna_id, SET_LNA_GAIN_COMMAND, 0, lna_gain_db)
    vga_id = next_request_id(lna_id)
    response = exchange_radio_request(
        sock, endpoint, build_set_gain_request(vga_id, SET_VGA_GAIN_COMMAND, 0, vga_gain_db)
    )
    validate_gain_stage_response(response, vga_id, SET_VGA_GAIN_COMMAND, 0, vga_gain_db)
    gain_read_id = next_request_id(vga_id)
    response = exchange_radio_request(
        sock, endpoint, build_get_value_request(gain_read_id, GET_GAIN_COMMAND, 0)
    )
    validate_signed_value_response(response, gain_read_id, GET_GAIN_COMMAND, 0)

    current_power_id = next_request_id(gain_read_id)
    response = exchange_radio_request(
        sock, endpoint, build_get_value_request(current_power_id, MEASURE_CURRENT_COMMAND, 0)
    )
    validate_signed_value_response(response, current_power_id, MEASURE_CURRENT_COMMAND, 0)

    raw_iq_id = next_request_id(current_power_id)
    response = exchange_radio_request(sock, endpoint, build_raw_iq_request(raw_iq_id, 0, 64))
    raw_iq = validate_raw_iq_response(
        response, raw_iq_id, 0, 64, frontend["iq_sample_format"]
    )

    frequency_measure_id = next_request_id(raw_iq_id)
    response = exchange_radio_request(
        sock, endpoint, build_measure_frequency_request(frequency_measure_id, 0, frequency_khz)
    )
    validate_signed_value_response(response, frequency_measure_id, MEASURE_FREQUENCY_COMMAND, 0)

    sweep_id = next_request_id(frequency_measure_id)
    response = exchange_radio_request(
        sock, endpoint, build_sweep_request(sweep_id, 0, frequency_khz,
                                            frequency_khz + 100, 100)
    )
    sweep_powers = validate_sweep_response(
        response, sweep_id, 0, [frequency_khz, frequency_khz + 100]
    )
    restore_id = next_request_id(sweep_id)
    response = exchange_radio_request(
        sock, endpoint, build_set_frequency_request(restore_id, 0, frequency_khz)
    )
    validate_frequency_response(response, restore_id, SET_FREQUENCY_COMMAND, 0, frequency_khz * 1000)
    return restore_id, {
        "sample_rate_hz": sample_rate_hz,
        "bandwidth_hz": bandwidth_hz,
        "gain_readback": True,
        "current_power": True,
        "raw_iq_bytes": len(raw_iq),
        "frequency_power": True,
        "sweep_points": len(sweep_powers),
    }


def build_exit_request(request_id):
    return struct.pack("<IB", request_id, EXIT_COMMAND)


def build_exit_response(request_id):
    return struct.pack("<IBB", request_id, EXIT_COMMAND, RADIO_STATUS_OK)


def next_request_id(request_id):
    return 1 if request_id == 0xFFFFFFFF else request_id + 1


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


def validate_exit_response(payload, expected_id):
    if len(payload) != 6:
        raise MockProtocolError("Exit response has {} bytes, expected 6".format(len(payload)))
    request_id, command, status = struct.unpack("<IBB", payload)
    if request_id != expected_id or command != EXIT_COMMAND or status != RADIO_STATUS_OK:
        raise MockProtocolError("Exit response id, command, or status mismatch")


def serve_one(sock, version, timeout, request_id, radio_commands=False, frontend_id=0,
              full_radio_commands=False):
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
        exit_after_id = request_id
        if radio_commands:
            query_id = next_request_id(request_id)
            sock.sendto(build_radio_frontends_request(query_id), client_endpoint)
            frontend_response, frontend_source = sock.recvfrom(65535)
            if frontend_source != source:
                raise MockProtocolError("radio frontend response came from an unexpected endpoint")
            radio_result = parse_radio_frontends_response(frontend_response, query_id)
            if not radio_result["frontends"]:
                raise MockProtocolError("no radio frontends found")
            if frontend_id not in {item["id"] for item in radio_result["frontends"]}:
                raise MockProtocolError("requested active frontend id is not available")

            frontend_ids = [frontend_id] + [item["id"] for item in radio_result["frontends"]
                                             if item["id"] != frontend_id]
            frontends_by_id = {item["id"]: item for item in radio_result["frontends"]}
            frequencies_hz = {}
            request_cursor = query_id
            for selected_id in frontend_ids:
                item = frontends_by_id[selected_id]
                select_id = next_request_id(request_cursor)
                sock.sendto(build_set_active_radio_request(select_id, selected_id), client_endpoint)
                select_response, select_source = sock.recvfrom(65535)
                if select_source != source:
                    raise MockProtocolError("active radio response came from an unexpected endpoint")
                validate_set_active_radio_response(select_response, select_id, selected_id)

                set_frequency_id = next_request_id(select_id)
                requested_frequency_khz = 100000
                sock.sendto(build_set_frequency_request(set_frequency_id, 0, requested_frequency_khz),
                            client_endpoint)
                set_frequency_response, set_frequency_source = sock.recvfrom(65535)
                if set_frequency_source != source:
                    raise MockProtocolError("SET_FREQUENCY response came from an unexpected endpoint")
                frequency_hz = requested_frequency_khz * 1000
                validate_frequency_response(set_frequency_response, set_frequency_id, SET_FREQUENCY_COMMAND, 0,
                                            frequency_hz)

                get_frequency_id = next_request_id(set_frequency_id)
                sock.sendto(build_get_frequency_request(get_frequency_id, 0), client_endpoint)
                get_frequency_response, get_frequency_source = sock.recvfrom(65535)
                if get_frequency_source != source:
                    raise MockProtocolError("GET_FREQUENCY response came from an unexpected endpoint")
                validate_frequency_response(get_frequency_response, get_frequency_id, GET_FREQUENCY_COMMAND, 0,
                                            frequency_hz)
                frequencies_hz[str(selected_id)] = frequency_hz
                if full_radio_commands:
                    restore_id, command_results = exercise_radio_commands(
                        sock, client_endpoint, get_frequency_id, item, requested_frequency_khz
                    )
                    radio_result.setdefault("commands_by_frontend", {})[str(selected_id)] = command_results
                else:
                    restore_id = get_frequency_id

                same_select_id = next_request_id(restore_id)
                sock.sendto(build_set_active_radio_request(same_select_id, selected_id), client_endpoint)
                same_select_response, same_select_source = sock.recvfrom(65535)
                if same_select_source != source:
                    raise MockProtocolError("same-frontend selection response came from an unexpected endpoint")
                validate_set_active_radio_response(same_select_response, same_select_id, selected_id)

                same_get_id = next_request_id(same_select_id)
                sock.sendto(build_get_frequency_request(same_get_id, 0), client_endpoint)
                same_get_response, same_get_source = sock.recvfrom(65535)
                if same_get_source != source:
                    raise MockProtocolError("frequency readback after same selection came from an unexpected endpoint")
                validate_frequency_response(same_get_response, same_get_id, GET_FREQUENCY_COMMAND, 0, frequency_hz)
                request_cursor = same_get_id

            neutral_id = next_request_id(request_cursor)
            sock.sendto(build_set_active_radio_request(neutral_id, 0xFF), client_endpoint)
            neutral_response, neutral_source = sock.recvfrom(65535)
            if neutral_source != source:
                raise MockProtocolError("neutral frontend selection came from an unexpected endpoint")
            validate_set_active_radio_response(neutral_response, neutral_id, 0xFF)
            inactive_frequency_id = next_request_id(neutral_id)
            sock.sendto(build_get_frequency_request(inactive_frequency_id, 0), client_endpoint)
            inactive_frequency_response, inactive_frequency_source = sock.recvfrom(65535)
            if inactive_frequency_source != source:
                raise MockProtocolError("inactive frequency response came from an unexpected endpoint")
            validate_frequency_response(inactive_frequency_response, inactive_frequency_id, GET_FREQUENCY_COMMAND,
                                        0, 0, expected_status=4)
            radio_result["frequencies_hz"] = frequencies_hz
            radio_result["neutral_closed"] = True
            exit_after_id = inactive_frequency_id

        exit_id = next_request_id(exit_after_id)
        sock.sendto(build_exit_request(exit_id), client_endpoint)
        exit_response, exit_source = sock.recvfrom(65535)
        if exit_source != source:
            raise MockProtocolError("Exit response came from an unexpected endpoint")
        validate_exit_response(exit_response, exit_id)
        if radio_result is not None:
            radio_result["exit_acknowledged"] = True
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
        "--full-radio-commands",
        action="store_true",
        help="also exercise settings, gain, power, raw-IQ, and sweep commands",
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
                        args.full_radio_commands,
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