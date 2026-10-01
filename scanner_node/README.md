# Orkestr Scanner Node

Portable C99 UDP client that performs the DemoServer handshake, answers VER, and exposes generic radio frontend capabilities. BladeRF and HackRF support is optional and enabled when their development libraries are available at build time.

## Network flow

1. Load and validate a device description JSON file with exactly 16 antenna entries.
2. Optionally write the loaded device data back to JSON with `--save-device-json`.
3. Bind one UDP socket to the configured local IPv4 address and port.
4. Serialize and send the 626-byte handshake explicitly as little-endian fields; native C struct layout is never sent directly.
5. Retry the handshake and accept only a 52-byte reply from the configured server endpoint.
6. Wait for a server-initiated 5-byte VER request `[request_id:u32 LE][0x01]`.
7. Reply with 14 bytes `[request_id:u32 LE][version:10 ASCII bytes, zero padded]`.
8. Answer `GET_RADIO_FRONTENDS` and `SET_ACTIVE_RADIO` using a vendor-neutral capability record.
9. Keep listening for subsequent radio commands until stopped; use `--command-timeout` for a finite test window.

Normally the session reply must be exactly 52 bytes. A temporary diagnostic build supports `--ignore-session-reply-size`: it accepts any datagram size from the configured server endpoint as the session-stage response, logs a warning, and proceeds to wait for VER. This does not decode or validate that packet and must not be treated as a successful handshake; the strict 52-byte check remains enabled by default.

The packet layouts follow `tools/Orkestr.DemoServer`'s `WireLayout` and `VerLayout`. The version default `1.0.0.0` follows the .NET SDK default assembly version for this project, which does not specify `Version` or `AssemblyVersion`; it has not been checked against a freshly built server here because only the .NET 8 SDK is installed while the project targets .NET 10. If the server assembly version changes, pass the matching value with `--software-version`; the server compares all ten version bytes exactly.

The session reply is length-checked and its sender endpoint is checked, but the key, nonce, and timestamp are not used because the current server does not require them for VER. This node does not authenticate the server cryptographically or implement encryption.

## Radio Frontend Commands

These experimental commands use little-endian `RequestId`; the C# DemoServer does not yet dispatch them. The Python mock supports them with `--radio-commands`. Commands are accepted only from the configured server endpoint. Unknown or malformed command datagrams are ignored.

### Command Frames

| Opcode | Name | Server-to-client request | Client-to-server response |
| --- | --- | --- | --- |
| `0x04` | `GET_RADIO_FRONTENDS` | 5 bytes: `request_id:u32 LE, opcode:u8` | `8 + count * 128` bytes: `request_id:u32 LE, opcode:u8, status:u8, count:u8, active_id:u8, descriptors[]` |
| `0x05` | `SET_ACTIVE_RADIO` | 6 bytes: `request_id:u32 LE, opcode:u8, frontend_id:u8` | 7 bytes: `request_id:u32 LE, opcode:u8, status:u8, active_id:u8` |

For `GET_RADIO_FRONTENDS`, status `0` means success. For `SET_ACTIVE_RADIO`, status `0` means selected, `2` means the ID is not present, and `1` is reserved for an invalid request. `active_id=0xff` means no frontend has been selected. The selection is held in process memory; it does not start a stream, tune hardware, or transmit RF. `--command-timeout 0` (default) waits indefinitely after VER; a positive value bounds that wait in milliseconds.

### Capability Descriptor

Each descriptor is exactly 128 bytes. Integers are unsigned unless explicitly marked signed; all multibyte fields are little-endian. Offsets below are relative to the start of one descriptor.

| Offset | Size | Field | Meaning and allowed values |
| ---: | ---: | --- | --- |
| 0 | 1 | `frontend_id` | Process-local ID; choose only an ID returned by this query. |
| 1 | 1 | `rx_channels` | Number of RX channels (`0..255`; current devices: 1 or 2). |
| 2 | 1 | `tx_channels` | Number of TX channels (`0..255`; current devices: 1 or 2). |
| 3 | 1 | `flags` | Bit 0 `FULL_DUPLEX`; other bits are reserved and zero. |
| 4 | 4 | `capabilities` | Bit 0 RX, bit 1 TX, bit 2 tune, bit 3 sample rate, bit 4 bandwidth, bit 5 gain. Unknown bits are reserved. |
| 8 | 8 | `frequency_min_hz` | Inclusive lower tuning limit. The scanner intentionally advertises a common floor of `70,000,000 Hz` for both backends. |
| 16 | 8 | `frequency_max_hz` | Inclusive upper tuning limit; currently `6,000,000,000 Hz` for both backends. |
| 24 | 4 | `frequency_step_hz` | Tuning input granularity in Hz; `0` means no uniform step is advertised. The actual tuned frequency may be quantized by the device. |
| 28 | 4 | `sample_rate_min_hz` | Inclusive minimum RX sample rate. |
| 32 | 4 | `sample_rate_max_hz` | Inclusive maximum RX sample rate. |
| 36 | 4 | `sample_rate_step_hz` | Uniform sample-rate step in Hz; `0` means no single step is advertised. |
| 40 | 4 | `bandwidth_min_hz` | Inclusive minimum RX filter bandwidth. |
| 44 | 4 | `bandwidth_max_hz` | Inclusive maximum RX filter bandwidth. |
| 48 | 4 | `bandwidth_step_hz` | Uniform bandwidth step in Hz; `0` means use the explicit options below, or that no uniform step is available. |
| 52 | 2 | `gain_min_cdb` | Signed minimum nominal RX gain in centi-dB (`-1500` means `-15.00 dB`). |
| 54 | 2 | `gain_max_cdb` | Signed maximum nominal RX gain in centi-dB. |
| 56 | 2 | `gain_step_cdb` | Uniform gain step in centi-dB; `0` means backend-mapped/nonuniform controls. |
| 58 | 1 | `sample_resolution_bits` | ADC resolution: currently `12` for bladeRF 2.0 micro, `8` for HackRF One. |
| 59 | 1 | `iq_sample_format` | `1` = signed interleaved 8-bit I/Q; `2` = signed interleaved 16-bit I/Q container. Resolution above remains the effective ADC precision. |
| 60 | 1 | `agc_modes` | Bit 0 hardware AGC; bit 1 software AGC. Current discovery advertises hardware AGC for BladeRF when supported; HackRF reports `0`. |
| 61 | 1 | `bandwidth_option_count` | Number of valid entries in `bandwidth_options` (`0..16`). |
| 62 | 1 | `antenna_paths` | Number of RX antenna paths currently exposed by the backend. |
| 63 | 1 | `reserved` | Must be zero; ignore on receive. |
| 64 | 64 | `bandwidth_options[16]` | Up to 16 exact RX bandwidths in Hz. The first `bandwidth_option_count` values are valid; remaining slots are zero. |

The HackRF bandwidth list is `1,750,000; 2,500,000; 3,500,000; 5,000,000; 5,500,000; 6,000,000; 7,000,000; 8,000,000; 9,000,000; 10,000,000; 12,000,000; 14,000,000; 15,000,000; 20,000,000; 24,000,000; 28,000,000 Hz`. BladeRF currently reports its API min/max/step and no explicit list. A zero step is not permission to choose outside the advertised range; the backend must validate and report the actual applied setting when a future configuration command is added.

Gain is one backend-neutral nominal RX scale in centi-dB. BladeRF limits come from libbladeRF's overall RX gain range. HackRF's advertised `0..11,300 centi-dB` is nominal combined RX gain (RF amp plus LNA/VGA); the backend must map a requested value to its hardware stages. HackRF RF amp gain varies by frequency, and its discrete stages mean `gain_step_cdb=0`. BladeRF gain limits can also vary with tuning frequency, so the final setter must re-check the range after tuning. `agc_modes` is a capability mask, not current AGC state.

### Current Backend Profiles

The descriptor reports runtime capabilities, not a hard-coded model ID. Expected profiles for the two current adapters are:

| Adapter | RX/TX and duplex | RX sample rate | RX bandwidth | Gain / AGC | IQ |
| --- | --- | --- | --- | --- | --- |
| bladeRF 2.0 micro | 2 RX, 2 TX, full duplex | libbladeRF range (about `521 kHz..61.44 MHz`) | libbladeRF range (about `200 kHz..56 MHz`); values may be quantized by the device | RX overall-gain range from libbladeRF (typically around `-15..+60 dB`, frequency-dependent); hardware AGC advertised when the API reports it | 12-bit converter, signed 16-bit I/Q container |
| HackRF One | 1 RX, 1 TX, half duplex | `2..20 MS/s` | exact 16-value list above | nominal RX total gain `0..113 dB`; nonuniform hardware stages; no hardware AGC | 8-bit signed I/Q |

The scanner deliberately reports `70 MHz` as the minimum for both radios even though HackRF One can tune lower and bladeRF TX can tune below its RX floor. This gives the server one common tuning domain. Sample-rate/bandwidth values are RX capabilities; TX tuning uses the same advertised common frequency domain. Zero step means the backend does not expose a uniform step; it does not mean all out-of-range values are accepted. Configuration commands must validate backend-specific values and return the actual applied setting.

`sample_resolution_bits` describes converter precision; `iq_sample_format` describes the signed I/Q component container. These are distinct: BladeRF carries 12-bit converter samples in a 16-bit component, while HackRF provides 8-bit components. Vendor/model/serial are deliberately absent from the wire response; IDs are discovery-order IDs and must be queried again after restart or reconnect.

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

### Optional SDR Discovery Build

The generic frontend inventory works without SDR libraries and reports zero devices. CMake automatically enables BladeRF and/or HackRF discovery when their headers and libraries are found. Discovery opens each available backend briefly to query hardware capabilities, then closes it. Frequency/rate/gain configuration, streaming, capture, measurements, and RF command handlers are not implemented yet.

Inspect which optional SDR libraries were linked into this build with:

```bash
ldd ./scanner_node | grep -Ei 'bladeRF|hackrf' || echo "No optional SDR backend linked"
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

The HackRF adapter currently discovers boards and advertises their generic capabilities. Keep libhackrf optional. Before adding tune/capture commands, test frequency/sample-rate/gain configuration and bounded RX capture with a known RF source; do not enable TX as part of these discovery tests.

## BladeRF Hardware Preparation

BladeRF is discovered through the same generic frontend abstraction as HackRF. Vendor identity appears only in local diagnostic output; the server receives channel counts and generic capabilities, not a model-specific command set.

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

The images were staged before the board model was known. The attached Pi board was later identified as a bladeRF 2.0 micro with a 49 KLE FPGA. Do not flash an FPGA image merely because it is available; the board reported a loaded FPGA during the latest read-only probe, so no firmware/FPGA write is needed for discovery.

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

Verified on `rpi4` at `10.123.71.141`: both a Nuand bladeRF 2.0 micro (`2cf0:5250`) and HackRF One (`1d50:6089`) are attached and accessible to `rpi`. BladeRF reports a loaded FPGA, firmware `2.4.0-git-a3d5c55f`, FPGA `0.14.0`, and SuperSpeed USB. `hackrf_info` identifies Board ID 2 (HackRF One), firmware `local-79baef7`, USB API 1.03. On 2026-10-01 the ARM scanner discovered two frontends; the Python mock queried both and selected HackRF logical ID `1` successfully. The test only enumerates/selects devices; it does not start RX/TX streaming.

### Firmware And FPGA Loading (Only After Model Identification)

`bladeRF-cli --help` documents `-f/--flash-firmware <file>`, `-l/--load-fpga <file>` (volatile load) and `-L/--flash-fpga <file>` (persistent FPGA flash). Use the matching image from `/usr/share/Nuand/bladeRF/` only when the CLI reports that an update/load is required and the exact board variant is known. FPGA flashing is persistent; a wrong image may prevent normal operation. Do not run firmware/FPGA writes as part of routine scanner tests.

The optional libbladeRF backend currently detects the device and queries its ranges. Actual frequency control, RX capture, streaming, and measurement commands still need implementation. `scanner_node/tools/radio_e2e_test.py` runs the real executable against the Python mock and exercises capability query plus logical frontend selection without transmitting RF.

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

With defaults, run the node directly; the server is `82.165.20.164` and the device JSON is `../scanner-node-build/device.json` relative to the current working directory. After VER it stays in the command loop until stopped:

```powershell
./scanner_node
```

Override either default with `--server` or `--device-json`. For example, to use the local mock server and the sample JSON:

```powershell
./scanner_node --server 127.0.0.1 --device-json ../../device.json --save-device-json ./device-copy.json
```

Options: `--server`, `--server-port`, `--bind-address`, `--local-port`, `--device-json`, `--save-device-json`, `--software-version`, `--attempts`, `--handshake-timeout`, `--ver-timeout`, and `--command-timeout`. Use `--command-timeout 1000` for a one-second test window.

## Tests

`scanner_protocol_tests` checks handshake layout, VER fields, radio command encoding, and JSON load/save round trip. `scanner_udp_integration_tests` performs handshake → session reply → server VER request → client VER response over loopback. Run `python tools/radio_e2e_test.py --client ./scanner_node --device-json ../device.json` from the deployed project directory to exercise the real binary against the Python mock and attached radio. It verifies discovery and selection only, not RF capture or measurement. The C# DemoServer does not yet dispatch the two experimental radio commands.