# Orkestr Scanner Node

Portable C99 UDP client that initiates the DemoServer handshake and answers its VER command. The implementation is isolated from the existing prototype in the parent directory.

## Network flow

1. Load and validate a device description JSON file with exactly 16 antenna entries.
2. Optionally write the loaded device data back to JSON with `--save-device-json`.
3. Bind one UDP socket to the configured local IPv4 address and port.
4. Serialize and send the 626-byte handshake explicitly as little-endian fields; native C struct layout is never sent directly.
5. Retry the handshake and accept only a 52-byte reply from the configured server endpoint.
6. Wait for a server-initiated 5-byte VER request `[request_id:u32 LE][0x01]`.
7. Reply with 14 bytes `[request_id:u32 LE][version:10 ASCII bytes, zero padded]`.
8. Exit after VER; later network commands are not implemented yet.

The packet layouts follow `tools/Orkestr.DemoServer`'s `WireLayout` and `VerLayout`. The version default `1.0.0.0` follows the .NET SDK default assembly version for this project, which does not specify `Version` or `AssemblyVersion`; it has not been checked against a freshly built server here because only the .NET 8 SDK is installed while the project targets .NET 10. If the server assembly version changes, pass the matching value with `--software-version`; the server compares all ten version bytes exactly.

The session reply is length-checked and its sender endpoint is checked, but the key, nonce, and timestamp are not used because the current server does not require them for VER. This node does not authenticate the server cryptographically or implement encryption or commands after VER.

## JSON files

Input/output uses the same schema as the original `device.c`:

```json
{
  "id": 12345,
  "version": 260911.01,
  "coord": { "lon": 2.154007, "lat": 41.3874 },
  "rfin": [
    {
      "in": 0, "from": 0, "to": 9, "polar": 0,
      "type": 0, "dBi": 10, "direction": 0
    }
  ]
}
```

`rfin` must contain exactly 16 objects. Integer fields are checked as signed 32-bit values; all number fields must be finite. The built-in `--save-device-json PATH` writes a normalized copy of the loaded device to the requested path. It is opt-in and does not modify the input file unless both paths name the same file.

## Build

Requirements: CMake 3.16+, C99 compiler, and the sibling `../cJSON` source already included in this repository.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

For Visual Studio/MSVC, omit `-G Ninja` and use:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake uses Winsock (`ws2_32`) on Windows and POSIX sockets on Linux/macOS.

## Python DemoServer mock

`tools/demo_server_mock.py` uses only the Python standard library. It validates a 626-byte handshake, sends a 52-byte session reply, issues a server-initiated VER request, and validates the client's 14-byte response including `RequestId` and all ten version bytes. It is a protocol test fixture, not a production server; it does not implement the device registry, command scheduling, cryptography, or later commands.

For a same-PC Windows test, start the mock in one terminal:

```powershell
python tools/demo_server_mock.py --bind-address 127.0.0.1 --port 2653 --clients 1
```

Then run the scanner from another terminal:

```powershell
cd build
./scanner_node.exe --server 127.0.0.1 --server-port 2653 --local-port 3333 --device-json ../../device.json
```

For a Raspberry Pi test, bind the mock to the PC's LAN interfaces and use the PC's LAN IPv4 as the scanner's `--server` value. Last verified on 2026-09-30: Windows PC `10.123.71.169`, Raspberry Pi `10.123.71.141`:

```powershell
python tools/demo_server_mock.py --bind-address 0.0.0.0 --port 2653 --clients 0
```

Allow inbound UDP port 2653 in Windows Firewall for the test. On the Pi, run from its deployed `scanner_node` directory:

```bash
./scanner_node --server 10.123.71.169 --server-port 2653 --local-port 3333 \
  --device-json ../device.json --software-version 1.0.0.0
```

These are the addresses observed during the successful LAN test; they may change if DHCP assigns different leases. Stop an indefinitely serving mock with Ctrl+C. The mock's `--version` must exactly match the version expected by the C# server/client under test.

Run the mock's protocol and local UDP tests with:

```powershell
python -m unittest discover -s tests -p test_demo_server_mock.py -v
```

## Run

From `scanner_node/build`, the default JSON path `../../device.json` points to the existing sample in the parent project:

```powershell
./scanner_node --server 127.0.0.1 --server-port 2653 --local-port 3333
```

On Windows, run `scanner_node.exe` with the same options. To load and save JSON explicitly:

```powershell
./scanner_node --server 127.0.0.1 --device-json ../../device.json --save-device-json ./device-copy.json
```

Options: `--server`, `--server-port`, `--bind-address`, `--local-port`, `--device-json`, `--save-device-json`, `--software-version`, `--attempts`, `--handshake-timeout`, and `--ver-timeout`. Use `--help` for current defaults.

## Tests

`scanner_protocol_tests` checks handshake layout, VER fields, and JSON load/save round trip. `scanner_udp_integration_tests` performs handshake → session reply → server VER request → client VER response over loopback. The Python mock tests the same flow and can also be used with the actual C executable or Pi. It remains a test fixture and does not replace validation against the production DemoServer and RF hardware.