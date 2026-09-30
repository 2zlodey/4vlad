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

Normally the session reply must be exactly 52 bytes. A temporary diagnostic build supports `--ignore-session-reply-size`: it accepts any datagram size from the configured server endpoint as the session-stage response, logs a warning, and proceeds to wait for VER. This does not decode or validate that packet and must not be treated as a successful handshake; the strict 52-byte check remains enabled by default.

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

## HackRF Hardware Setup

### Default Hardware-Free Build

The default `scanner_node` target is network-only: `CMakeLists.txt` does not include or link libhackrf, and the executable does not open the SDR. It can be built and used for JSON, handshake, and VER debugging even on a system without HackRF packages or hardware. Installing libhackrf on the Pi does not change this binary. RF configuration, capture, measurement, and corresponding network command handlers are not implemented in `scanner_node` yet.

Verify that the deployed/default executable remains independent of libhackrf with:

```bash
ldd ./scanner_node | grep -i hackrf || echo "No libhackrf dependency (network-only build)"
./scanner_node --help
```

### Install And Configure HackRF On Raspberry Pi

On the verified Ubuntu 22.04 ARM Pi, the following packages were available and installed:

```bash
sudo apt-get update
sudo apt-get install -y hackrf libhackrf-dev
```

`hackrf` supplies `hackrf_info`/`hackrf_sweep`; `libhackrf-dev` supplies the headers and link library, and depends on the runtime `libhackrf0`. The package installs `/lib/udev/rules.d/60-libhackrf0.rules`, including a HackRF One rule for USB VID/PID `1d50:6089`, mode `0660`, group `plugdev`.

Check the connected device and determine its current USB node/sysfs name (bus/device numbers can change after reconnect):

```bash
lsusb -d 1d50:6089
DEVICE_NODE=$(lsusb -d 1d50:6089 | sed -n 's/Bus \([0-9]*\) Device \([0-9]*\):.*/\/dev\/bus\/usb\/\1\/\2/p')
UDEV_PATH=$(udevadm info --query=path --name="$DEVICE_NODE")
USB_SYSNAME=${UDEV_PATH##*/}
printf 'device=%s sysname=%s\n' "$DEVICE_NODE" "$USB_SYSNAME"
```

If the device was already plugged in during package installation, reload the packaged rule and trigger only that USB device:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger --action=add --subsystem-match=usb --sysname-match="$USB_SYSNAME"
sudo udevadm settle
ls -l "$DEVICE_NODE"
id -nG
```

Expected: the USB node is `root:plugdev` with mode `crw-rw----`, and `rpi` belongs to `plugdev`. If the group was newly added to the user, start a new login session before testing. On the verified Pi, `hackrf_info` then opened the radio as `rpi` without sudo and reported HackRF One, firmware `n_230411`, API `1.07`.

Run a one-shot receive-only smoke test (no RF transmission):

```bash
hackrf_info
hackrf_sweep -f 88:108 -w 1000000 -1
```

Expected: `hackrf_info` reports the board and firmware; `hackrf_sweep` prints measurement bins for 88–108 MHz and exits after one sweep. This tests USB/libhackrf RX access, not the scanner's command handlers.

### Future Hardware Integration

When RF commands are added, keep libhackrf optional: a hardware-enabled CMake build should explicitly discover/link libhackrf, while the default network-only build remains independent. Hardware initialization errors should not prevent the network-only handshake/VER debugging path. Test the hardware backend separately with device detection, frequency/sample-rate/gain configuration, bounded RX capture, and a known RF source before wiring command opcodes to it.

## BladeRF Hardware Preparation

BladeRF is a separate SDR backend from HackRF. The default `scanner_node` executable does not link libbladeRF and does not require a BladeRF to be connected; this keeps handshake/VER server debugging available regardless of radio hardware. Installing the BladeRF packages below prepares the Pi OS but does not add radio commands to this executable.

### Packages And Prepared Images

On the Ubuntu 22.04 ARM Pi, the following packages were installed:

```bash
sudo apt-get update
sudo apt-get install -y bladerf libbladerf-dev \
  bladerf-firmware-fx3 \
  bladerf-fpga-hostedx40 bladerf-fpga-hostedx115 \
  bladerf-fpga-hostedxa4 bladerf-fpga-hostedxa5 bladerf-fpga-hostedxa9
```

This provides `bladeRF-cli` 1.8.0, libbladeRF 2.4.1 and development headers. The package post-install scripts downloaded and verified these files under `/usr/share/Nuand/bladeRF/`:

| File | Intended hardware |
| --- | --- |
| `bladeRF_fw.img` | FX3 firmware; shared by supported bladeRF boards |
| `hostedx40.rbf` | bladeRF1 with the 40KLE FPGA |
| `hostedx115.rbf` | bladeRF1 with the 115KLE FPGA |
| `hostedxA4.rbf` | bladeRF2 xA4 |
| `hostedxA5.rbf` | bladeRF2 xA5 |
| `hostedxA9.rbf` | bladeRF2 xA9 |

All variants are staged because the board is currently disconnected and its exact model/FPGA is unknown. Do not flash an FPGA image based only on the USB VID/PID; identify the board first. Package installation downloads files but does not write firmware/FPGA images to the radio.

### USB Permissions

The Ubuntu package installs udev rules for bladeRF1 (`2cf0:5246`), legacy bladeRF1 (`1d50:6066`), bladeRF2 (`2cf0:5250`) and the Cypress FX3 bootloader (`04b4:00f3`) with `TAG+="uaccess"`. For reliable access from an SSH session, this repository also contains `tools/99-nuand-bladerf-rpi.rules`, which grants mode `0660` to group `plugdev` for those IDs. It was installed on the Pi as `/etc/udev/rules.d/99-nuand-bladerf-rpi.rules`; `rpi` is already a member of `plugdev`.

To reproduce or refresh the local rule, copy it from the repository root and install it on the Pi:

```powershell
scp scanner_node/tools/99-nuand-bladerf-rpi.rules rpi@10.123.71.141:/tmp/
ssh rpi@10.123.71.141 "sudo install -m 0644 /tmp/99-nuand-bladerf-rpi.rules /etc/udev/rules.d/99-nuand-bladerf-rpi.rules"
ssh rpi@10.123.71.141 "rm -f /tmp/99-nuand-bladerf-rpi.rules"
ssh -tt rpi@10.123.71.141 "sudo udevadm control --reload-rules"
```

After plugging/replugging the board, check USB enumeration, node permissions, and CLI access:

```bash
lsusb | grep -Ei 'Nuand|bladeRF'
ls -l /dev/bus/usb/*/*
id -nG
bladeRF-cli --version
bladeRF-cli -p
```

Expected with a connected, accessible board: `bladeRF-cli -p` lists it. Currently the Pi reports `No devices are available`, which is expected because the BladeRF is unplugged. The package CLI and libraries are present; device-specific initialization cannot be validated until it is connected.

### Firmware And FPGA Loading (Only After Model Identification)

`bladeRF-cli --help` documents `-f/--flash-firmware <file>`, `-l/--load-fpga <file>` (volatile load) and `-L/--flash-fpga <file>` (persistent FPGA flash). Use the matching image from `/usr/share/Nuand/bladeRF/` only when the CLI reports that an update/load is required and the exact board variant is known. FPGA flashing is persistent; a wrong image may prevent normal operation. Do not run firmware/FPGA writes as part of routine scanner tests.

The CMake project currently has no optional BladeRF backend or libbladeRF link target. The BladeRF CLI can validate hardware independently, but actual frequency control, RX capture, streaming, and protocol commands still need to be implemented and tested in a dedicated backend.

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
scp scanner_node/tools/demo_server_mock.py scanner_node/tools/99-nuand-bladerf-rpi.rules rpi@10.123.71.141:/mnt/scaner-ram/orkestr-scanner/scanner_node/tools/
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

### 4a. Diagnose A Nonstandard Session Reply

For diagnosis only, a separate build can accept a non-52-byte datagram from the configured endpoint and continue waiting for VER. On the Pi the diagnostic executable is `~/vlad/4vlad/scanner_node.ignore-session-size`; the strict default remains `~/vlad/4vlad/scanner_node`:

```bash
cd ~/vlad/4vlad
./scanner_node.ignore-session-size \
  --server 89.129.2.140 --server-port 3333 --local-port 3333 \
  --device-json /home/vlad/4vlad/device.json --software-version 1.0.0.0 \
  --attempts 1 --handshake-timeout 5000 --ver-timeout 5000 \
  --ignore-session-reply-size
```

Observed on 2026-09-30 during an earlier diagnostic: configuring `89.129.2.140:3333` produced 626-byte datagrams and no VER request. This was a different endpoint/port from the production server configuration below, not evidence that the strict handshake is incompatible. Do not use the diagnostic executable for normal operation; retain the strict binary as default.

### Deployment Notes And Issues Seen

- The first binary copied from the 32-bit `armv7l` Pi was ARM EABI/armhf. The remote `np.lora-wan.net` Pi reports `aarch64` and has only `/lib/ld-linux-aarch64.so.1`, so it could not execute that binary (`ld-linux-armhf.so.3` was absent). Rebuild natively for the target; the current strict binary is AArch64 at `/home/vlad/vlad/4vlad/scanner_node`, and the earlier armhf build is preserved as `scanner_node.armhf`.
- The target host had native GCC but no CMake. The scanner was compiled as C99 directly from its sources. GCC terminated silently while compiling the vendored `cJSON.c` with the initial optimized one-command build; preprocessing/syntax checks passed and compiling that translation unit separately with `-O0 -fno-inline -fno-builtin` succeeded. The resulting binary passed JSON/protocol and UDP loopback tests before installation.
- The earlier diagnostic test against `89.129.2.140:3333` received 626-byte datagrams and did not reach VER. That result was superseded by the successful production-server test below, which used `82.165.20.164:2653`.
- `scanner_node.ignore-session-size` is intentionally separate from the strict executable. It does not decode or validate the nonstandard payload. Keep the ordinary `scanner_node` as the default and use the diagnostic variant only to investigate the remote server protocol.
- The deployment directory `/home/vlad/vlad/4vlad` contains the binary only. Supply the device JSON path explicitly (for example `/home/vlad/4vlad/device.json`) when that file exists on the target host.

### 5. Verify Against The Real C# DemoServer

Use a .NET 10 SDK. Note that the current checkout is missing the project referenced by `tools/Orkestr.DemoServer/Orkestr.csproj`: `tools/common/Orkestr.Common.Logging/Orkestr.Common.Logging.csproj`. Restore that dependency before expecting a source build to work. Once it is present, from the `4vlad` root query the built server version and run the server in separate terminals:

```powershell
dotnet run --project tools/Orkestr.DemoServer/Orkestr.csproj -- --version
dotnet run --project tools/Orkestr.DemoServer/Orkestr.csproj -- --listen-address 0.0.0.0 --listen-port 2653
```

Pass the exact version printed by `--version` as `--software-version` to the Pi scanner. The mock's default `1.0.0.0` is only a default and is not proof of the real server's current version.

Successful production-server exchange verified on 2026-09-30 from the remote Raspberry Pi:

```bash
./scanner_node --server 82.165.20.164 --device-json ../scanner-node-build/device.json
```

The scanner sent a 626-byte handshake, accepted the 52-byte session reply, answered VER request `3` with a 14-byte `1.0.0.0` response, and exited successfully. The server logged the device ID `12345`, sent the session reply, issued VER request `3`, accepted the matching response, and reported `Application channel is ready`. It observed the client's public/NAT endpoint as `89.129.2.140:3333`; the configured server destination was `82.165.20.164:2653`. This verifies the strict handshake/session/VER path against the production server. It does not verify RF commands, which are not implemented yet.

If a later run reports `INCOMPATIBLE_VERSION` or times out, verify the server's expected assembly version, IPv4 endpoint, UDP/2653 firewall rules, and that the C# server is listening.

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

With defaults, run the node directly; the server is `82.165.20.164` and the device JSON is `../scanner-node-build/device.json` relative to the current working directory:

```powershell
./scanner_node
```

Override either default with `--server` or `--device-json`. For example, to use the local mock server and the sample JSON:

```powershell
./scanner_node --server 127.0.0.1 --device-json ../../device.json --save-device-json ./device-copy.json
```

Options: `--server`, `--server-port`, `--bind-address`, `--local-port`, `--device-json`, `--save-device-json`, `--software-version`, `--attempts`, `--handshake-timeout`, and `--ver-timeout`. Use `--help` for current defaults.

## Tests

`scanner_protocol_tests` checks handshake layout, VER fields, and JSON load/save round trip. `scanner_udp_integration_tests` performs handshake → session reply → server VER request → client VER response over loopback. The Python mock tests the same flow and can also be used with the actual C executable or Pi. It remains a test fixture and does not replace validation against the production DemoServer and RF hardware.