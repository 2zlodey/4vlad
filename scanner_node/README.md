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

## Manual Deployment And Full Check

Run these steps from the `4vlad` repository root unless a command says otherwise. The Raspberry Pi deployment verified on 2026-09-30 used `rpi@10.123.71.141` and its existing writable tmpfs at `/mnt/scaner-ram` (512 MiB). RAM-disk contents disappear at reboot. Check the current mount and Windows/Pi addresses before reusing the commands; DHCP may change them.

### 1. Validate On Windows

From `scanner_node`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p test_demo_server_mock.py -v
```

Expected: both CTest cases and all five Python tests pass.

### 2. Copy Sources To The Pi RAM Disk

Check that the RAM disk exists and is writable; this deployment does not need sudo when that is true:

```powershell
ssh rpi@10.123.71.141 "findmnt /mnt/scaner-ram; test -w /mnt/scaner-ram && echo RAM_DISK_WRITABLE"
ssh rpi@10.123.71.141 "mkdir -p /mnt/scaner-ram/orkestr-scanner/scanner_node /mnt/scaner-ram/orkestr-scanner/cJSON"
ssh rpi@10.123.71.141 "mkdir -p /mnt/scaner-ram/orkestr-scanner/scanner_node/tools"
scp -r scanner_node/include scanner_node/src scanner_node/tests scanner_node/CMakeLists.txt scanner_node/README.md rpi@10.123.71.141:/mnt/scaner-ram/orkestr-scanner/scanner_node/
scp scanner_node/tools/demo_server_mock.py rpi@10.123.71.141:/mnt/scaner-ram/orkestr-scanner/scanner_node/tools/
scp -r cJSON device.json rpi@10.123.71.141:/mnt/scaner-ram/orkestr-scanner/
```

If `/mnt/scaner-ram` is not mounted, create a temporary 512 MiB tmpfs (contents will be lost on reboot):

```bash
sudo mkdir -p /mnt/scaner-ram
sudo mount -t tmpfs -o size=512M,mode=0755,uid=$(id -u),gid=$(id -g) tmpfs /mnt/scaner-ram
findmnt /mnt/scaner-ram
test -w /mnt/scaner-ram && echo RAM_DISK_WRITABLE
```

The `sudo mount` step may prompt for the Pi account password. It is not needed when the prepared RAM disk is already mounted and writable.

The commands above intentionally do not copy local `build/` output: the Pi compiles natively for ARM. SSH/SCP prompts for the Pi account password interactively.

### 3. Build And Run Native Tests On The Pi

The verified Pi image has GCC but not CMake/Make, so use the direct C99 build from the deployed project directory:

```bash
cd /mnt/scaner-ram/orkestr-scanner/scanner_node
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic \
  -Iinclude -I../cJSON \
  src/main.c src/device_config.c src/protocol.c src/udp_socket.c \
  ../cJSON/cJSON.c -lm -o scanner_node

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic \
  -Iinclude -I../cJSON \
  tests/protocol_tests.c src/device_config.c src/protocol.c ../cJSON/cJSON.c \
  -lm -o scanner_protocol_tests
./scanner_protocol_tests ../device.json ./device-roundtrip.json

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic \
  -Iinclude tests/udp_integration_tests.c src/udp_socket.c src/protocol.c \
  -lm -o scanner_udp_integration_tests
./scanner_udp_integration_tests
./scanner_node --help
```

Expected: `scanner_protocol_tests` exits 0 after JSON load/save/load and packet checks; the UDP test prints `Handshake/session/VER UDP exchange passed.`; `file scanner_node` reports a 32-bit ARM EABI executable. No root privileges are needed for local port 3333 or the mock port 2653.

### 4. Verify Pi To Windows Mock Across The LAN

The last successful LAN test used Windows Wi-Fi `10.123.71.169` and Pi `10.123.71.141`. Recheck with `ipconfig` and `hostname -I` if addresses have changed. Check the Wi-Fi firewall category with `Get-NetConnectionProfile`; in an Administrator PowerShell, add the rule for that category (`Private`, `Public`, or `Domain`), then start the mock from `scanner_node`:

```powershell
Get-NetConnectionProfile | Format-Table InterfaceAlias, NetworkCategory
```

```powershell
New-NetFirewallRule -DisplayName "Orkestr UDP mock 2653" -Direction Inbound -Protocol UDP -LocalPort 2653 -Action Allow -Profile Private
```

Replace `Private` in the rule with the Wi-Fi profile shown by the preceding command if they differ.

```powershell
python tools/demo_server_mock.py --bind-address 0.0.0.0 --port 2653 --version 1.0.0.0 --timeout 30 --clients 1
```

Allow inbound UDP/2653 through Windows Firewall for the active network profile. Then, on the Pi:

```bash
cd /mnt/scaner-ram/orkestr-scanner/scanner_node
./scanner_node --server 10.123.71.169 --server-port 2653 --local-port 3333 \
  --device-json ../device.json --software-version 1.0.0.0 \
  --attempts 1 --handshake-timeout 5000 --ver-timeout 3000
```

Pass criteria on the Pi: `Session reply received` and `VER 1 answered with version 1.0.0.0`. Pass criteria in the Windows mock: `OK device_id=12345 ... client=10.123.71.141:3333 reply_port=3333 VER id=1 version='1.0.0.0'`. Exit status from the Pi command should be 0. This proves network reachability, both directions, packet sizes, reply port, request correlation, and version encoding; it does not test RF hardware. Remove the temporary rule afterwards:

```powershell
Remove-NetFirewallRule -DisplayName "Orkestr UDP mock 2653"
```

### 5. Verify Against The Real C# DemoServer

Use a .NET 10 SDK. Note that the current checkout is missing the project referenced by `tools/Orkestr.DemoServer/Orkestr.csproj`: `tools/common/Orkestr.Common.Logging/Orkestr.Common.Logging.csproj`. Restore that dependency before expecting a source build to work. Once it is present, from the `4vlad` root query the built server version and run the server in separate terminals:

```powershell
dotnet run --project tools/Orkestr.DemoServer/Orkestr.csproj -- --version
dotnet run --project tools/Orkestr.DemoServer/Orkestr.csproj -- --listen-address 0.0.0.0 --listen-port 2653
```

Pass the exact version printed by `--version` as `--software-version` to the Pi scanner. The server should log `Application channel is ready`; the scanner should report that it answered VER. If it instead reports `INCOMPATIBLE_VERSION` or times out, verify the assembly version, IPv4 endpoint, UDP/2653 firewall rules, and that the C# server is actually listening. The mock's default `1.0.0.0` is only a default and is not proof of the real server's current version.

If handshake times out, first verify both addresses and listeners, then allow UDP/2653 inbound on Windows. The client deliberately ignores replies from any endpoint other than the exact configured server IP and port.

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

For LAN testing, add/remove the temporary Windows Firewall rule as shown in step 4 above.

On the Pi, run from its deployed `scanner_node` directory:

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