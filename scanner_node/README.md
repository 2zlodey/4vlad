# Orkestr Scanner Node

Portable C99 UDP client that performs the DemoServer handshake, answers VER, exposes generic radio frontend capabilities, configures RX, and returns bounded I/Q and power measurements. BladeRF and HackRF support is optional and enabled when their development libraries are available at build time.

## Network flow

1. Load and validate a device description JSON file with exactly 16 antenna entries.
2. Optionally write the loaded device data back to JSON with `--save-device-json`.
3. Bind one UDP socket to the configured local IPv4 address and port.
4. Serialize and send the 626-byte handshake explicitly as little-endian fields; native C struct layout is never sent directly.
5. Retry the handshake and accept only a 52-byte reply from the configured server endpoint.
6. Wait for a server-initiated 5-byte VER request `[request_id:u32 LE][0x01]`.
7. Reply with 14 bytes `[request_id:u32 LE][version:10 ASCII bytes, zero padded]`.
8. Answer `GET_RADIO_FRONTENDS` and `SET_ACTIVE_RADIO` using a vendor-neutral capability record.
9. Keep listening for radio commands after VER; close normally only after an acknowledged `EXIT` command.

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
| `0x64` | `SET_FREQUENCY` | 10 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, frequency_khz:u32 LE` | 15 bytes: `request_id:u32 LE, opcode:u8, status:u8, RX channel:u8, applied_frequency_hz:u64 LE` |
| `0x6a` | `GET_FREQUENCY` | 6 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8` | 15 bytes: `request_id:u32 LE, opcode:u8, status:u8, RX channel:u8, frequency_hz:u64 LE` |
| `0x65` | `SET_SAMPLE_RATE` | 10 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, sample_rate_hz:u32 LE` | 11 bytes: `request_id:u32 LE, opcode:u8, status:u8, RX channel:u8, applied_hz:u32 LE` |
| `0x6c` | `GET_SAMPLE_RATE` | 6 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8` | Same 11-byte setting response; status `3` means not configured. |
| `0x68` | `SET_BANDWIDTH` | 10 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, bandwidth_hz:u32 LE` | Same 11-byte setting response. |
| `0x66`, `0x67` | `SET_LNA_GAIN`, `SET_VGA_GAIN` | 7 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, gain_db:u8` | 8 bytes: `request_id:u32 LE, opcode:u8, status:u8, RX channel:u8, applied_db:u8` |
| `0x6b` | `GET_GAIN` | 6 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8` | 9 bytes: `request_id:u32 LE, opcode:u8, status:u8, RX channel:u8, total_gain_cdb:i16 LE` |
| `0x6d` | `MEASURE_CURRENT` | 6 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8` | 9 bytes with signed `power_cdbfs:i16 LE` carrying the DSP noise-floor estimate. |
| `0x02` | `MEASURE_FREQUENCY` | 10 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, frequency_khz:u32 LE` | Same 9-byte result; tunes first, then captures and estimates noise floor. |
| `0x03` | `SWEEP` | 18 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, start_khz:u32 LE, stop_khz:u32 LE, step_khz:u32 LE` | `9 + count*6` bytes: header `[request_id:u32 LE, opcode, status, channel, count:u16 LE]`, followed by `[frequency_khz:u32 LE, power_cdbfs:i16 LE]` points; `power_cdbfs` contains the DSP noise-floor estimate. |
| `0x70` | `GET_RAW_IQ` | 8 bytes: `request_id:u32 LE, opcode:u8, RX channel:u8, complex_pairs:u16 LE` | 10-byte header `[request_id:u32 LE, opcode, status, channel, format:u8, complex_pairs:u16 LE]` followed by interleaved I/Q bytes. |
| `0x06` | `EXIT` | 5 bytes: `request_id:u32 LE, opcode:u8` | 6 bytes: `request_id:u32 LE, opcode:u8, status:u8` |

For `GET_RADIO_FRONTENDS`, status `0` means success. For `SET_ACTIVE_RADIO`, status `0` means selected and `2` means the ID is not present. For `EXIT`, status `0` confirms that the client accepted the shutdown request. Radio status values are `0` success, `1` invalid, `2` not found, `3` not configured, `4` no active frontend, `5` invalid RX channel, `6` outside range or unsupported discrete value, `7` backend operation failed, `8` operation unsupported, and `9` capture failed. `active_id=0xff` means no frontend has been selected. The selection is held in process memory; selecting it alone does not start a stream or transmit RF.

Frequency commands use a zero-based RX channel. `SET_FREQUENCY` accepts whole kHz and converts to Hz internally; it validates the active frontend's range, tunes RX only, and returns applied frequency in Hz. `GET_FREQUENCY` performs libbladeRF hardware readback on BladeRF. HackRF has no frequency-get API, so it returns the last frequency successfully requested by this process; before the first successful SET it returns status `3` (`NOT_CONFIGURED`). Other statuses are `4` no active/open frontend, `5` invalid RX channel, `6` outside the advertised range, and `7` backend operation failed.

Selecting the currently active frontend leaves its open handle untouched. Selecting another valid frontend closes the previous device handle before opening the new one. `frontend_id=0xff` is the neutral selection: it closes all open device handles and clears the active ID, responding with status `0` and active ID `0xff`. Exit closes any remaining handles after sending its ACK.

`SET_SAMPLE_RATE` and `SET_BANDWIDTH` use unsigned Hz and report hardware readback where available. Bandwidth must match an advertised explicit option when the frontend provides a list. Gain commands set backend-specific LNA/VGA stages in integer dB; `GET_GAIN` returns the sum in signed centi-dB. The internal DSP module computes a Hann-windowed complex Welch PSD with 50% overlap, removes per-segment DC, and reports `dBFS/Hz` bins. FFT size is a power of two from 64 through 4096; `MEASURE_*` uses 512 bins on 1024 samples and `SWEEP` uses 1024 bins on 4096 samples. Feature extraction uses median spectral floor, an 8 dB above-floor threshold, contiguous-bin band edges, occupied bandwidth, peak offset, and spectral flatness. Zero-valued spectral bins use a documented `-180 dBFS/Hz` numerical floor; when the median hits that floor, a peak-relative fallback prevents pure-tone FFT zeros from inflating the estimated floor.

For compatibility, the existing `power_cdbfs` server field remains mean normalized window power (`10*log10(mean((I^2+Q^2)/FS^2))`) in signed centi-dBFS, not the Welch median floor. The separate PSD floor and signal bands are currently local analysis results; the server protocol is unchanged. This scalar does not distinguish broadband noise from a coherent signal and is not calibrated dBm. Frequency measurement leaves the radio tuned to the requested frequency. Sweep includes an explicit step (an extension to the source catalog, which only listed start/stop), permits at most 128 points, runs the same PSD analyzer for each point, and leaves the radio at its final point. RAW I/Q is capped at 4096 complex pairs and bypasses DSP, returning the captured buffer unchanged. Format `1` is signed 8-bit I/Q; format `2` is signed 16-bit little-endian I/Q components. RX operations have a 2-second capture bound. BladeRF reports its tuned frequency readback; HackRF has no frequency getter and reports the last successfully requested frequency. These values are not RF calibration measurements.

The source catalog did not define the wire fields for these operations, so this implementation fixes them as shown above: a 4-byte little-endian RequestId precedes every radio opcode; settings use Hz; gain stage values use dB; measurements use centi-dBFS; and the new raw-IQ opcode is `0x70`. Unknown or malformed datagrams are ignored. The C# DemoServer does not yet dispatch these opcodes; the Python mock supports the extended set with `--full-radio-commands`.

After a valid VER response, the client remains in its UDP command loop indefinitely. `SET_ACTIVE_RADIO` opens the selected device and keeps its handle for later commands; switching frontend or selecting `0xff` closes the old handle. Exit sends its ACK, closes all radio handles and the socket, then returns success. There is no idle timeout; malformed and unknown datagrams are ignored. A fatal socket error can still terminate the process with failure.

DSP calculations are isolated in `scanner_dsp`; they consume an I/Q byte buffer plus its format and sample count, and do not depend on SDR handles, sockets, or protocol structs. `scanner_analysis` owns a bounded copy queue and a separate analysis thread; the radio worker submits a captured window and receives its feature/classification result without sharing device handles with DSP. Raw-IQ and processed-result commands remain separate paths: raw data bypasses analysis, while measure/sweep use PSD and the generic classifier. `scanner_classifier` is a plugin-style API whose initial feature rules only label narrowband-tone, multicarrier-like, or wideband-noiselike spectra; ambiguous/unsupported signals remain `UNKNOWN`. These are coarse feature labels, not an exhaustive modulation/protocol recognizer or a trained model.

For a focused real BladeRF band-edge survey, build and run `tools/bladerf_band_capture.c` on the Pi. It refuses Stub/HackRF-only inventory, captures 4096 complex samples per tune at 2 MS/s, steps overlapping 1 MHz tune windows around the requested fundamental and its second/third harmonics, then merges Welch detections into absolute-frequency CSV edges. The default fundamental is 1660 MHz with +/-40 MHz search span. Edges are threshold-based estimates (8 dB above per-window median PSD), with resolution set by the 1024-point FFT and tune step; rerun with a narrower tune step or larger FFT/window to refine them. This diagnostic is local-only and does not change server protocol.

### Thread Ownership

After VER, the UDP/network thread owns the socket, validates the peer endpoint, queues radio requests, and sends all radio responses. A single radio worker owns the mutable radio inventory and device handles; it processes requests serially through bounded request/result queues (8 entries each). This keeps long sweeps and captures out of the socket loop without allowing concurrent access to a radio handle. `EXIT` cancels an active sweep between capture points, discards queued radio work, returns its ACK through the network thread, and then closes the radio worker. Individual hardware captures remain bounded by a 2-second timeout; shutdown can wait for an in-flight backend capture to return.

### Performance Probes

`perf_lib` is built as a separate library and uses a host HAL: `QueryPerformanceCounter` on Windows and `CLOCK_MONOTONIC`/pthread critical sections on Linux. `SCANNER_ENABLE_PERF` defaults to `ON`; disable it with `-DSCANNER_ENABLE_PERF=OFF` to compile the scope macros to no-ops. The radio command handler records command latency, and the benchmark reports aggregate total-sweep and per-frequency timing.

`scanner_sweep_benchmark` measures a complete receive-and-process sweep on HackRF and BladeRF serially. The default is `1000..6000 MHz` at `10 MHz` steps (501 points per radio), `2 MS/s`, `1.75 MHz` bandwidth, and 4096 complex samples plus Welch/classifier analysis per point. Run only with both radios connected and a receive-safe antenna/load:

```powershell
build\scanner_sweep_benchmark.exe 1000 6000 10
```

On the Pi, build the tool with the native flags in the deployment section, then run `./scanner_sweep_benchmark 1000 6000 10`. Output includes wall-clock sweep seconds and `perf_lib` min/average/max per-point times. This measures the current RX tune + capture + DSP/classifier path; it is not just FFT CPU time.

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

The HackRF bandwidth list is `1,750,000; 2,500,000; 3,500,000; 5,000,000; 5,500,000; 6,000,000; 7,000,000; 8,000,000; 9,000,000; 10,000,000; 12,000,000; 14,000,000; 15,000,000; 20,000,000; 24,000,000; 28,000,000 Hz`. BladeRF currently reports its API min/max/step and no explicit list. Setters validate advertised ranges/options and report the applied setting; hardware may quantize a request.

Gain capability limits are nominal RX scale in centi-dB. The stage-setting wire commands are integer dB and backend-specific: HackRF exposes its discrete LNA/VGA steps; BladeRF maps to libbladeRF's `LNA` and `VGA1` stages. `GET_GAIN` reports total gain, not the per-stage values. `agc_modes` is a capability mask, not current AGC state.

### Current Backend Profiles

The descriptor reports runtime capabilities, not a hard-coded model ID. Expected profiles for the two current adapters are:

| Adapter | RX/TX and duplex | RX sample rate | RX bandwidth | Gain / AGC | IQ |
| --- | --- | --- | --- | --- | --- |
| bladeRF 2.0 micro | 2 RX, 2 TX, full duplex | libbladeRF range (about `521 kHz..61.44 MHz`) | libbladeRF range (about `200 kHz..56 MHz`); values may be quantized by the device | RX overall-gain range from libbladeRF (typically around `-15..+60 dB`, frequency-dependent); hardware AGC advertised when the API reports it | 12-bit converter, signed 16-bit I/Q container |
| HackRF One | 1 RX, 1 TX, half duplex | `2..20 MS/s` | exact 16-value list above | nominal RX total gain `0..113 dB`; nonuniform hardware stages; no hardware AGC | 8-bit signed I/Q |

The scanner deliberately reports `70 MHz` as the minimum for both radios even though HackRF One can tune lower and bladeRF TX can tune below its RX floor. This gives the server one common tuning domain. Sample-rate/bandwidth values are RX capabilities; TX tuning uses the same advertised common frequency domain. Zero step means the backend does not expose a uniform step; it does not mean all out-of-range values are accepted.

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

### Stub SDR For Server Development

The CMake option `SCANNER_ENABLE_STUB_SDR` is enabled by default. If the compiled physical backends find no radio, discovery adds one in-memory `Stub SDR`; when a real HackRF or BladeRF is found, the stub is not added. Disable it with `-DSCANNER_ENABLE_STUB_SDR=OFF` to test the explicit no-radio case.

The stub advertises one 8-bit RX channel with RX, tune, sample-rate, bandwidth, and gain capabilities over the shared `70 MHz..6 GHz` tuning range. It is a deterministic signal simulator, not a control-only placeholder: every capture repeats the same four-pair S8 I/Q cycle, scaled by a fixed piecewise-linear synthetic power profile over frequency. Repeating a request at the same tuned frequency produces the same I/Q bytes; changing frequency changes only the deterministic amplitude, not the cycle shape. Shorter captures are prefixes of longer captures. Stub sweeps therefore show repeatable synthetic peaks and dips instead of a flat trace. The profile is test data, not an RF propagation model or a measurement of received energy. No stub path transmits RF.

Run the full network plus radio-command round-trip against the Python mock without SDR libraries or hardware:

```powershell
python tools/demo_server_mock.py --bind-address 127.0.0.1 --port 2653 --radio-commands --clients 1
```

In another terminal, from the `scanner_node` directory:

```powershell
tools/radio_e2e_test.py --client build/scanner_node.exe --device-json ../device.json
```

Expected: the scanner reports one `Stub SDR`; the E2E checks settings/readback, gain, raw I/Q, both power operations, a two-point sweep, neutral close, and EXIT, then prints `E2E PASS`. The mock supplies a test capability response and validates the returned wire layouts. With optional physical backends compiled and hardware detected, discovery uses the real devices and does not add the stub fallback.

### Optional SDR Discovery Build

The generic frontend inventory works without SDR libraries; by default it falls back to the stub described above. CMake automatically enables BladeRF and/or HackRF support when their headers and libraries are found. Discovery opens each available backend briefly to query hardware capabilities, then closes it. Selecting a frontend opens and retains its handle; switching or neutral selection closes it. RX sample-rate, bandwidth, gain-stage, bounded capture, power, and sweep commands are implemented for the optional real backends. TX remains unsupported and is never enabled by these commands.

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

### Remaining Hardware Integration

The HackRF adapter discovers boards, advertises generic capabilities, and supports RX frequency/sample-rate/bandwidth/gain plus bounded S8 capture and power measurements. Keep libhackrf optional. Hardware streaming should still be smoke-tested on the target Pi with the current libhackrf version and a known RF source; do not enable TX as part of these tests.

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

The optional libbladeRF backend detects the device, queries its ranges, opens/closes a persistent selected handle, configures RX frequency/sample rate/bandwidth/gain, and captures bounded SC16_Q11 samples for power and raw-IQ commands. `scanner_node/tools/radio_e2e_test.py` runs the real executable against the Python mock and exercises the complete command set; run it against real hardware only when RX streaming is safe for the bench setup. TX is not implemented.

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
`SCANNER_ENABLE_STUB_SDR` defaults to `ON`; build with `-DSCANNER_ENABLE_STUB_SDR=OFF` only when an empty frontend inventory is specifically desired.

## Passwordless SSH To Raspberry Pi

Key-based SSH is configured for `rpi@10.123.71.141` with the dedicated Windows key `%USERPROFILE%\.ssh\rpi_scanner_ed25519`. The private key has no passphrase for unattended deployment; keep it private and do not add it to this repository. To reproduce on another workstation, create an Ed25519 key, press Enter twice at the passphrase prompts if unattended use is required, copy only the `.pub` file to the Pi, and append that one line to `~/.ssh/authorized_keys` with directory/file permissions `700/600`. The first key installation requires the Pi account password; normal login does not.

Verify key-only access with:

```powershell
ssh -i "$HOME\.ssh\rpi_scanner_ed25519" -o IdentitiesOnly=yes -o BatchMode=yes rpi@10.123.71.141 "id -un; hostname"
```

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

Expected: both CTest cases and all six Python tests pass.

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
gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic -pthread \
  -DCONFIG_PERF_ENABLE=1 -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -DSCANNER_ENABLE_STUB_SDR \
  -Iinclude -I../cJSON \
  -Iperf_lib src/main.c src/radio_worker.c src/analysis_worker.c src/device_config.c src/protocol.c src/udp_socket.c \
  src/radio_frontend.c src/dsp.c src/signal_classifier.c perf_lib/perf_probe.c \
  perf_lib/perf_probe_hal_host.c ../cJSON/cJSON.c \
  -lm -lbladeRF -lhackrf -o scanner_node

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic \
  -Iinclude -I../cJSON \
  -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -DSCANNER_ENABLE_STUB_SDR \
  tests/protocol_tests.c src/device_config.c src/protocol.c src/radio_frontend.c src/dsp.c \
  ../cJSON/cJSON.c -lm -lbladeRF -lhackrf -o scanner_protocol_tests
./scanner_protocol_tests ../device.json ./device-roundtrip.json

gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -Iinclude \
  tests/dsp_tests.c src/dsp.c src/signal_classifier.c -lm -o scanner_dsp_tests
./scanner_dsp_tests

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude \
  tests/analysis_worker_tests.c src/analysis_worker.c src/dsp.c src/signal_classifier.c \
  -lm -o scanner_analysis_worker_tests
./scanner_analysis_worker_tests

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic -pthread \
  -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -DSCANNER_ENABLE_STUB_SDR \
  -Iinclude -I../cJSON tests/radio_worker_tests.c src/radio_worker.c src/radio_frontend.c src/dsp.c \
  -lm -lbladeRF -lhackrf -o scanner_radio_worker_tests
./scanner_radio_worker_tests

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic \
  -Iinclude tests/udp_integration_tests.c src/udp_socket.c src/protocol.c \
  -lm -o scanner_udp_integration_tests
./scanner_udp_integration_tests

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -DCONFIG_PERF_ENABLE=1 -O2 -Wall -Wextra -Wpedantic -pthread \
  -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude -Iperf_lib \
  tools/sdr_sweep_benchmark.c src/radio_frontend.c src/analysis_worker.c src/dsp.c \
  src/signal_classifier.c perf_lib/perf_probe.c perf_lib/perf_probe_hal_host.c \
  -lm -lbladeRF -lhackrf -o sdr_sweep_benchmark
./scanner_node --help
```

gcc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic -pthread \
  -DSCANNER_HAVE_BLADERF -DSCANNER_HAVE_HACKRF -Iinclude \
  tools/bladerf_band_capture.c src/radio_frontend.c src/dsp.c src/signal_classifier.c \
  -lm -lbladeRF -lhackrf -o bladerf_band_capture

./sdr_sweep_benchmark 1000 6000 10

Expected: protocol, DSP/classifier, analysis-worker, and radio-worker tests exit 0; the UDP test prints `Handshake/session/VER UDP exchange passed.`; `file scanner_node` reports a 32-bit ARM EABI executable. No root privileges are needed for local port 3333 or the mock port 2653.

After a receive-only capture is safe for the connected antenna/bench, run the focused BladeRF survey:

```bash
./bladerf_band_capture ./av1660_harmonics.csv 1660 40 1000
```

Arguments are output CSV, fundamental MHz, half-span MHz, and tune step kHz. The default examines 1620-1700 MHz, 3280-3360 MHz, and 4940-5020 MHz. Do not treat the generic classifier label as proof of an AV protocol; inspect the PSD/edge rows and compare against the known transmitter setup.

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

The one-shot client used for this historical test sent a 626-byte handshake, accepted the 52-byte session reply, answered VER request `3` with a 14-byte `1.0.0.0` response, and then exited. The server logged device ID `12345`, accepted VER, and reported `Application channel is ready`. It observed the client's public/NAT endpoint as `89.129.2.140:3333`; the configured server destination was `82.165.20.164:2653`. The current client continues listening after VER and waits for `EXIT` (`0x06`); the C# DemoServer does not yet send this command. This historical log verifies handshake/session/VER only, not radio commands.

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

With defaults, run the node directly; the server is `82.165.20.164` and the device JSON is `../scanner-node-build/device.json` relative to the current working directory. After VER it stays in the command loop until the server sends `EXIT`:

```powershell
./scanner_node
```

Override either default with `--server` or `--device-json`. For example, to use the local mock server and the sample JSON:

```powershell
./scanner_node --server 127.0.0.1 --device-json ../../device.json --save-device-json ./device-copy.json
```

Options: `--server`, `--server-port`, `--bind-address`, `--local-port`, `--device-json`, `--save-device-json`, `--software-version`, `--attempts`, `--handshake-timeout`, and `--ver-timeout`.

## Tests

`scanner_protocol_tests` checks handshake layout, VER fields, frontend/frequency command encoding, Exit ACK, stub control behavior when enabled, and JSON load/save round trip. `scanner_udp_integration_tests` performs handshake → session reply → server VER request → client VER response over loopback.

For a no-hardware end-to-end radio command test, start the Python mock with `--radio-commands` as shown in **Stub SDR For Server Development**, then run `tools/radio_e2e_test.py --client build/scanner_node.exe --device-json ../device.json` from the `scanner_node` directory. When the stub is enabled (default), the client should report one stub frontend. The test tunes each reported frontend to 100 MHz, reads it back, switches/neutral-closes it, and exits via ACK. No RX/TX stream is started. With physical backends enabled and radios attached, the same E2E tool exercises detected devices. The C# DemoServer does not yet dispatch these experimental radio commands.