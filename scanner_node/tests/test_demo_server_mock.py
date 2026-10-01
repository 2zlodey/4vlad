import pathlib
import socket
import struct
import sys
import threading
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.demo_server_mock import (  # noqa: E402
    HANDSHAKE_SIZE,
    MockProtocolError,
    build_frequency_response,
    build_get_frequency_request,
    build_radio_frontends_request,
    build_radio_frontends_response,
    build_exit_request,
    build_exit_response,
    build_set_active_radio_request,
    build_set_active_radio_response,
    build_set_frequency_request,
    build_ver_request,
    build_ver_response,
    parse_handshake,
    serve_one,
    parse_radio_frontends_response,
    validate_ver_response,
)


class DemoServerMockTests(unittest.TestCase):
    def test_parses_scanner_handshake_layout(self):
        packet = bytearray(HANDSHAKE_SIZE)
        struct.pack_into("<IqqH", packet, 0, 7, 123, 456, 3333)
        struct.pack_into("<i", packet, 22, 12345)

        handshake = parse_handshake(packet)

        self.assertEqual(handshake["counter"], 7)
        self.assertEqual(handshake["reply_port"], 3333)
        self.assertEqual(handshake["device_id"], 12345)

    def test_rejects_short_handshake(self):
        with self.assertRaises(MockProtocolError):
            parse_handshake(b"short")

    def test_ver_request_and_response_are_little_endian_and_padded(self):
        request = build_ver_request(0x12345678)
        response = build_ver_response(0x12345678, "1.0.0.0")

        self.assertEqual(request, b"\x78\x56\x34\x12\x01")
        self.assertEqual(response, b"\x78\x56\x34\x12" + b"1.0.0.0\0\0\0")
        validate_ver_response(response, 0x12345678, "1.0.0.0")

    def test_rejects_wrong_ver_id_or_version(self):
        response = build_ver_response(8, "1.0.0.0")
        with self.assertRaises(MockProtocolError):
            validate_ver_response(response, 9, "1.0.0.0")
        with self.assertRaises(MockProtocolError):
            validate_ver_response(response, 8, "1.0.0.1")

    def test_udp_handshake_session_and_ver_exchange(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        client.bind(("127.0.0.1", 0))
        server.settimeout(2)
        client.settimeout(2)
        server_result = []
        server_error = []

        def run_server():
            try:
                server_result.append(serve_one(server, "1.0.0.0", 2, 1))
            except Exception as error:  # Propagate worker failure to the test thread.
                server_error.append(error)

        worker = threading.Thread(target=run_server)
        worker.start()
        try:
            client_port = client.getsockname()[1]
            packet = bytearray(HANDSHAKE_SIZE)
            struct.pack_into("<IqqH", packet, 0, 1, 10, 20, client_port)
            struct.pack_into("<i", packet, 22, 12345)
            client.sendto(packet, server.getsockname())

            session, _ = client.recvfrom(1024)
            request, _ = client.recvfrom(1024)
            self.assertEqual(len(session), 52)
            self.assertEqual(request, build_ver_request(1))
            client.sendto(build_ver_response(1, "1.0.0.0"), server.getsockname())
            exit_request, _ = client.recvfrom(1024)
            self.assertEqual(exit_request, build_exit_request(2))
            client.sendto(build_exit_response(2), server.getsockname())
        finally:
            worker.join(timeout=3)
            client.close()
            server.close()

        self.assertFalse(worker.is_alive(), "mock server thread did not stop")
        self.assertEqual(server_error, [])
        self.assertEqual(server_result[0][0]["device_id"], 12345)

    def test_udp_exchange_queries_and_selects_generic_radio_frontend(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        client.bind(("127.0.0.1", 0))
        server.settimeout(2)
        client.settimeout(2)
        server_result = []
        server_error = []

        def run_server():
            try:
                server_result.append(serve_one(server, "1.0.0.0", 2, 7, True, 0))
            except Exception as error:  # Propagate worker failure to the test thread.
                server_error.append(error)

        worker = threading.Thread(target=run_server)
        worker.start()
        try:
            client_port = client.getsockname()[1]
            packet = bytearray(HANDSHAKE_SIZE)
            struct.pack_into("<IqqH", packet, 0, 1, 10, 20, client_port)
            struct.pack_into("<i", packet, 22, 12345)
            client.sendto(packet, server.getsockname())

            session, _ = client.recvfrom(1024)
            request, _ = client.recvfrom(1024)
            self.assertEqual(len(session), 52)
            self.assertEqual(request, build_ver_request(7))
            client.sendto(build_ver_response(7, "1.0.0.0"), server.getsockname())

            query, _ = client.recvfrom(1024)
            self.assertEqual(query, build_radio_frontends_request(8))
            response = build_radio_frontends_response(8)
            client.sendto(response, server.getsockname())
            inventory = parse_radio_frontends_response(response, 8)
            self.assertEqual(inventory["frontends"][0]["rx_channels"], 2)
            self.assertEqual(inventory["frontends"][0]["tx_channels"], 2)
            self.assertEqual(inventory["frontends"][0]["frequency_min_hz"], 70000000)
            self.assertEqual(inventory["frontends"][0]["frequency_max_hz"], 6000000000)
            self.assertEqual(inventory["frontends"][0]["sample_resolution_bits"], 12)
            self.assertEqual(inventory["frontends"][0]["iq_sample_format"], 2)
            self.assertEqual(inventory["frontends"][0]["agc_modes"], 1)
            self.assertEqual(inventory["frontends"][0]["bandwidth_options"], [1750000, 2500000])

            selection, _ = client.recvfrom(1024)
            self.assertEqual(selection, build_set_active_radio_request(9, 0))
            client.sendto(build_set_active_radio_response(9, 0), server.getsockname())

            set_frequency, _ = client.recvfrom(1024)
            self.assertEqual(set_frequency, build_set_frequency_request(10, 0, 100000))
            client.sendto(build_frequency_response(10, 0x64, 0, 0, 100000000), server.getsockname())

            get_frequency, _ = client.recvfrom(1024)
            self.assertEqual(get_frequency, build_get_frequency_request(11, 0))
            client.sendto(build_frequency_response(11, 0x6A, 0, 0, 100000000), server.getsockname())

            same_selection, _ = client.recvfrom(1024)
            self.assertEqual(same_selection, build_set_active_radio_request(12, 0))
            client.sendto(build_set_active_radio_response(12, 0), server.getsockname())
            same_get, _ = client.recvfrom(1024)
            self.assertEqual(same_get, build_get_frequency_request(13, 0))
            client.sendto(build_frequency_response(13, 0x6A, 0, 0, 100000000), server.getsockname())

            neutral, _ = client.recvfrom(1024)
            self.assertEqual(neutral, build_set_active_radio_request(14, 0xFF))
            client.sendto(build_set_active_radio_response(14, 0xFF), server.getsockname())
            inactive_get, _ = client.recvfrom(1024)
            self.assertEqual(inactive_get, build_get_frequency_request(15, 0))
            client.sendto(build_frequency_response(15, 0x6A, 4, 0, 0), server.getsockname())

            exit_request, _ = client.recvfrom(1024)
            self.assertEqual(exit_request, build_exit_request(16))
            client.sendto(build_exit_response(16), server.getsockname())
        finally:
            worker.join(timeout=3)
            client.close()
            server.close()

        self.assertFalse(worker.is_alive(), "mock server thread did not stop")
        self.assertEqual(server_error, [])
        self.assertEqual(server_result[0][2]["frontends"][0]["id"], 0)
        self.assertTrue(server_result[0][2]["exit_acknowledged"])
        self.assertTrue(server_result[0][2]["neutral_closed"])
        self.assertEqual(server_result[0][2]["frequencies_hz"], {"0": 100000000})


if __name__ == "__main__":
    unittest.main()