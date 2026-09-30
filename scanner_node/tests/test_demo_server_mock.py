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
    build_ver_request,
    build_ver_response,
    parse_handshake,
    serve_one,
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
        finally:
            worker.join(timeout=3)
            client.close()
            server.close()

        self.assertFalse(worker.is_alive(), "mock server thread did not stop")
        self.assertEqual(server_error, [])
        self.assertEqual(server_result[0][0]["device_id"], 12345)


if __name__ == "__main__":
    unittest.main()