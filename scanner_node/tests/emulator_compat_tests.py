#!/usr/bin/env python3
"""Exercise actual scanner C binary using the supplied emulator's wire layouts.
Requires a stub-only build (no physical SDR). No external server is contacted.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import unittest

CLIENT = None

class EmulatorCompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        folder = Path(self.temp.name)
        device = {'id': 12345, 'version': 260911.01, 'coord': {'lon': 2.65, 'lat': 39.57},
                  'rfin': [dict(zip(('in', 'from', 'to', 'polar', 'type', 'dBi', 'direction'),
                                   (i, 0, 0, 0, 0, 0, 0))) for i in range(16)]}
        config = folder / 'device.json'
        config.write_text(json.dumps(device))
        self.sockets = []
        self.server = self.new_socket()
        self.commands = self.new_socket()
        self.recording = folder / 'iq.bin'
        self.log = open(folder / 'scanner.log', 'w+')
        self.addCleanup(self.log.close)
        self.process = subprocess.Popen(
            [CLIENT, '--server', '127.0.0.1', '--server-port', str(self.server.getsockname()[1]),
             '--local-port', str(self.free_port()), '--device-json', str(config),
             '--iq-file', str(self.recording), '--attempts', '1', '--debug', '1'],
            stdout=self.log, stderr=subprocess.STDOUT)
        self.addCleanup(self.stop)
        handshake, self.peer = self.server.recvfrom(4096)
        self.assertEqual(len(handshake), 626)
        self.assertEqual(struct.unpack_from('<H', handshake, 20)[0], self.peer[1])
        # Session and VER deliberately originate from ports different from server_port.
        self.commands.sendto(bytes(53), self.peer)
        if self._testMethodName == "test_measurement_before_ver":
            return
        self.commands.sendto(struct.pack('<IB', 0x12345678, 1), self.peer)
        self.assertEqual(self.commands.recvfrom(4096)[0],
                         struct.pack('<I', 0x12345678) + b'1.1.0.0\0\0\0')

    def free_port(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0))
            return sock.getsockname()[1]

    def new_socket(self, address='127.0.0.1'):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((address, 0))
        sock.settimeout(3)
        self.addCleanup(sock.close)
        return sock

    def stop(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=3)

    def exchange(self, packet, sock=None):
        sock = sock or self.commands
        sock.sendto(packet, self.peer)
        return sock.recvfrom(65535)[0]

    def test_measurement_before_ver(self):
        response = self.exchange(struct.pack('<IBBI', 61, 2, 0, 100000))
        self.assertEqual(len(response), 8)
        self.assertEqual(struct.unpack_from('<I', response)[0], 61)
        self.assertEqual(self.exchange(struct.pack('<IB', 62, 1)),
                         struct.pack('<I', 62) + b'1.1.0.0\0\0\0')

    def test_measurements_without_radio_setup(self):
        # No GET_RADIO_FRONTENDS/SET_ACTIVE/SET_RATE/SET_GAIN was sent.
        response = self.exchange(struct.pack('<IBBI', 17, 2, 0, 100000))
        self.assertEqual(len(response), 8)
        rid, opcode, channel, power = struct.unpack('<IBBh', response)
        self.assertEqual((rid, opcode, channel), (17, 2, 0))
        self.assertLess(power, 0)
        response = self.exchange(struct.pack('<IBBII', 18, 3, 0, 100000, 100100))
        self.assertEqual(len(response), 12)
        rid, opcode, channel, frequency, power = struct.unpack('<IBBIh', response)
        self.assertEqual((rid, opcode, channel), (18, 3, 0))
        self.assertEqual(frequency, 100000)  # descending stub power; ties choose first
        self.assertLess(power, 0)
        response = self.exchange(struct.pack('<IBBII', 19, 3, 0, 100000, 100000))
        self.assertEqual(struct.unpack('<IBBIh', response)[:4], (19, 3, 0, 100000))

    def test_each_queued_reply_uses_its_own_source_port(self):
        other = self.new_socket()
        self.commands.sendto(struct.pack('<IBBI', 31, 2, 0, 100000), self.peer)
        other.sendto(struct.pack('<IBBI', 32, 2, 0, 300000), self.peer)
        self.assertEqual(struct.unpack_from('<I', self.commands.recvfrom(4096)[0])[0], 31)
        self.assertEqual(struct.unpack_from('<I', other.recvfrom(4096)[0])[0], 32)
        self.assertEqual(self.exchange(struct.pack('<IB', 33, 1), other),
                         struct.pack('<I', 33) + b'1.1.0.0\0\0\0')
        self.assertEqual(self.exchange(struct.pack('<IB', 34, 0xde), other),
                         struct.pack('<IBB', 34, 0xde, 0))
        self.assertEqual(self.process.wait(timeout=3), 0)

    def test_foreign_ip_and_unsupported_channel_do_not_create_measurements(self):
        foreign = self.new_socket('127.0.0.2')
        foreign.settimeout(0.15)
        foreign.sendto(struct.pack('<IB', 41, 1), self.peer)
        with self.assertRaises(socket.timeout):
            foreign.recvfrom(4096)
        self.commands.settimeout(0.15)
        self.commands.sendto(struct.pack('<IBBI', 42, 2, 1, 100000), self.peer)
        with self.assertRaises(socket.timeout):
            self.commands.recvfrom(4096)
        self.commands.settimeout(3)
        self.assertEqual(len(self.exchange(struct.pack('<IBBI', 43, 2, 0, 100000))), 8)

    def test_large_range_is_bounded_and_includes_stop(self):
        response = self.exchange(struct.pack('<IBBII', 51, 3, 0, 300000, 450000))
        self.assertEqual(len(response), 12)
        frequency = struct.unpack('<IBBIh', response)[3]
        self.assertTrue(300000 <= frequency <= 450000)
        data = self.recording.read_bytes()
        self.assertEqual(data[:8], b'SCIQREC1')
        offset, centers = 8, []
        while offset < len(data):
            self.assertEqual(data[offset:offset+4], b'IQRF')
            centers.append(struct.unpack_from('<Q', data, offset + 8)[0])
            payload_size = struct.unpack_from('<I', data, offset + 28)[0]
            offset += 40 + payload_size
        self.assertLessEqual(len(centers), 128)
        self.assertEqual(len(centers), 128)
        self.assertEqual((centers[0], centers[-1]), (300000000, 450000000))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True)
    args, remaining = parser.parse_known_args()
    CLIENT = str(Path(args.client).resolve())
    unittest.main(argv=[__file__] + remaining)
