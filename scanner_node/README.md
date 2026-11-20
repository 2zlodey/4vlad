# Orkestr Scanner Node

Portable C99 UDP client for the Orkestr handshake, VER exchange, and SDR commands. BladeRF/HackRF backends are optional; the Stub backend supports tests without hardware.

## Wire Protocol

All multibyte fields are little-endian. Extended radio commands use the common headers below. Standard VER, 0x02 and 0x03 use the emulator layouts.

| Frame | Layout | Size |
| --- | --- | ---: |
| Handshake | Device handshake fields | 626 B |
| Session reply | `[key:32][nonce:12][timestamp:8]` | 52 B |
| VER request | `[request_id:u32 LE][opcode:0x01]` | 5 B |
| VER response | `[request_id:u32 LE][version:10 ASCII bytes, zero-padded]` | 14 B |
| Radio request | `[request_id:u32][opcode:u8][arguments]` | varies |
| Radio response | `[request_id:u32][opcode:u8][status:u8][fields]` | varies |

VER echoes RequestId and has no opcode or status in its response. Standard measurement replies also omit status. Radio command arguments keep the units shown below; capability descriptors are in Hz.

## Commands

| Opcode | Command | Request after common header | Response fields |
| --- | --- | --- | --- |
| `0x04` | `GET_RADIO_FRONTENDS` | none | `status:u8, count:u8, active_id:u8, descriptors[]` (`8 + count*128` B total) |
| `0x05` | `SET_ACTIVE_RADIO` | `frontend_id:u8` | `status:u8, active_id:u8` |
| `0x64` | `SET_FREQUENCY` | `channel:u8, frequency_khz:u32` | `status:u8, channel:u8, applied_frequency_khz:u32` |
| `0x6A` | `GET_FREQUENCY` | `channel:u8` | `status:u8, channel:u8, frequency_khz:u32` |
| `0x65` | `SET_SAMPLE_RATE` | `channel:u8, sample_rate_khz:u32` | `status:u8, channel:u8, applied_khz:u32` |
| `0x6C` | `GET_SAMPLE_RATE` | `channel:u8` | `status:u8, channel:u8, sample_rate_khz:u32` |
| `0x68` | `SET_BANDWIDTH` | `channel:u8, bandwidth_hz:u32` | `status:u8, channel:u8, bandwidth_hz:u32` |
| `0x66/0x67` | `SET_LNA_GAIN/SET_VGA_GAIN` | `channel:u8, gain_db:u8` | `status:u8, channel:u8, applied_db:u8` |
| `0x6B` | `GET_GAIN` | `channel:u8` | `status:u8, channel:u8, total_gain_cdb:i16` |
| `0x6E` | `GET_GAIN_STAGES` | `channel:u8` | `status:u8, channel:u8, lna_db:i8, vga_db:i8` |
| `0x6D` | `MEASURE_CURRENT` | `channel:u8` | `status:u8, channel:u8, power_cdbfs:i16` |
| `0x02` | `MEASURE_FREQUENCY` | `channel:u8, frequency_khz:u32` | `channel:u8, power_cdbfs:i16` (8 B total) |
| `0x03` | `MEASURE_FREQUENCY_RANGE` | `channel:u8, start_khz:u32, stop_khz:u32` | `channel:u8, frequency_khz:u32, power_cdbfs:i16` (12 B total) |
| `0x70` | `GET_RAW_IQ` | `channel:u8, complex_pairs:u16` | `status:u8, channel:u8, format:u8, complex_pairs:u16, IQ bytes[]` |
| `0x71` | `SAVE_IQ_TO_FILE` | `channel:u8, complex_pairs:u16` | `status:u8, channel:u8, format:u8, complex_pairs:u16, payload_bytes:u32` |
| `0x72` | `DETECT_START` | `channel:u8, antenna_id:u8, algorithm_id:u8, start/stop_khz:u32, dwell_ms:u32, algorithm params` | `status:u8, job_id:u32, effective_antenna_id:u8` |
| `0x73` | `DETECT_GET_RESULT` | `job_id:u32, candidate_offset:u32` | job state/progress and up to 64 `{center_hz:u64, bandwidth_hz:u32, power_cdbfs:i16, snr_cdb:i16}` candidates |
| `0xDE` | `EXIT` | none | `status:u8` |
| `0x69` | `REINITIALIZE_SDR` | none | `status:u8` |
| `0xC8` | `SELECT_ANTENNA` | `board_channel:u8, antenna:u8` | `status:u8, board_channel:u8, applied_antenna:u8` |
| `0xC9` | `SET_PATH` | `board_channel:u8, value:u8` | `status:u8, board_channel:u8, power_cdb:i16` |

`0x64`, `0x65`, `0x6A`, `0x6C`, measurement, and sweep frequencies/rates use kHz. `0x68` uses Hz. Backend settings and advertised capability ranges remain in Hz. Frequency/sample-rate readbacks are rounded to the nearest kHz.

## Status Codes

| Code | Meaning |
| ---: | --- |
| `0` | Success |
| `1` | Invalid request |
| `2` | Frontend not found |
| `3` | Not configured |
| `4` | No active frontend |
| `5` | Invalid RX channel |
| `6` | Out of range or unsupported option |
| `7` | Hardware error |
| `8` | Unsupported operation |
| `9` | Capture failed |
| `10` | Detector job already running |
| `11` | Detector candidate capacity reached; returned pages are partial |

`SET_ACTIVE_RADIO` also uses `2` when the frontend ID is absent. `active_id=0xff` means no frontend selected.

## Radio Behavior

| Area | Behavior |
| --- | --- |
| RX channel | Zero-based; supported channels are `0` and `1`. |
| Reinitialize (`0x69`) | Closes/reopens the active backend and clears cached settings. Returns `4` with no active radio, `7` on reopen failure. |
| Antenna/gain path (`0xC8/0xC9`) | `0xC8` validates the logical antenna ID and acknowledges it; physical switching is deferred. `0xC9` remains a placeholder returning `8`. |
| Gain | Total gain is signed centi-dB. Gain-stage values are signed dB; BladeRF reports total RX gain in LNA and `0` in VGA. |
| DSP | Welch PSD, Hann window, 50% overlap; FFT sizes 64-4096. Measurement uses 1024 samples; sweep uses 4096. |
| Detector | `0x72/0x73`, internal overlapped RF windows, algorithm 1 CA-CFAR, all candidates retrieved using offsets. See `DETECTION.md` for the step-by-step server procedure, wire layouts and parameters. |
| BladeRF detector capture | Uses `BLADERF_RX_X1` for the selected channel only. Either RX channel can use its advertised 61.44 Msps maximum independently; analog BW max is 56 MHz. |
| Antenna routing | `rfin[].in` is the antenna ID. Optional `antennaRouting` configures each of two RX channels as `switched` or `fixed`. Physical switch control is deferred; logical selection is acknowledged. Units for `rfin[].from/to` remain unspecified and are not used to reject RF ranges. |
| IQ | Maximum 4096 complex pairs. Format `1`: signed 8-bit I/Q. Format `2`: signed 16-bit little-endian I/Q. |

## Logging

Use `--debug N` (default `1`).

| Mode | Output |
| ---: | --- |
| `0` | Startup and fatal errors |
| `1` | Events to console |
| `2` | TXT log; startup/fatal errors to console |
| `3` | HTML log; startup/fatal errors to console |

Log files are created in the working directory as `scanner_YYYYMMDD_HHMMSS_PID_SEQUENCE.txt` or `.html`. Entries include local timestamp and module; packet logs include endpoint, size, and relevant request/response fields. IQ payload samples are not logged.

## FFTW Builds

The DSP backend uses FFTW3. Windows MinGW builds use the `x64-mingw-static` vcpkg triplet; Linux builds use the system `fftw3` pkg-config module. Cross builds need target-architecture FFTW development libraries and the matching pkg-config wrapper.

| Target | Configure example |
| --- | --- |
| Windows MinGW x64 | `cmake -S . -B build-fftw -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_HOST_TRIPLET=x64-mingw-static` |
| Linux native | `cmake -S . -B build-linux -G Ninja` |
| ARMHF | `cmake -S . -B build-armhf -G Ninja -DCMAKE_TOOLCHAIN_FILE=devtools/toolchains/armhf-linux.cmake` |
| AArch64 | `cmake -S . -B build-aarch64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=devtools/toolchains/aarch64-linux.cmake` |

Install FFTW3 development packages first (`fftw3` via vcpkg on Windows, `libfftw3-dev` on Debian/Ubuntu). ARM builds require the matching cross compiler and target `libfftw3-dev` package. FFTW is GPL-2.0-or-later; distributing a linked scanner binary may require GPL compliance. Obtain legal review or a suitable commercial FFTW license before proprietary distribution.

The project keeps the existing native behavior and FFTW's forward-transform convention; FFTW planner operations are serialized across callers. Existing PSD/tone tests cover the frequency-bin ordering and normalization.

## Run and Test

| Task | Command (from `scanner_node`) |
| --- | --- |
| Configure | `cmake -S . -B build -G Ninja` |
| Build | `cmake --build build` |
| C tests | `ctest --test-dir build --output-on-failure` |
| Python protocol tests | `python -m unittest discover -s tests -p test_demo_server_mock.py -v` |
| Logging E2E | `python tools/logging_e2e_test.py --client build/scanner_node.exe --device-json ../device.json` |
| Radio E2E | `python tools/radio_e2e_test.py --client build/scanner_node.exe --device-json ../device.json` |
| Run | `build/scanner_node.exe --server 127.0.0.1 --server-port 2653 --local-port 3333 --device-json ../device.json` |

For a local mock, start `python tools/demo_server_mock.py --bind-address 127.0.0.1 --port 2653 --clients 1` in another terminal. Add `--radio-commands` for command E2E tests. The mock is a test fixture; it is not the production C# DemoServer.

## Options

| Option | Purpose |
| --- | --- |
| `--server`, `--server-port` | Server IPv4 address and UDP port |
| `--bind-address`, `--local-port` | Local UDP bind endpoint |
| `--device-json`, `--save-device-json` | Load/save device configuration |
| `--iq-file` | Write captured I/Q to an append-only `SCIQREC1` file |
| `--software-version` | Set the 10-byte VER response version |
| `--attempts`, `--handshake-timeout`, `--ver-timeout` | Configure retry and timeout behavior |
| `--debug` | Select logging mode `0`-`3` |

## Emulator compatibility (2026-11-07)

- Initial 626-byte handshake is unchanged. Session replies must contain at least 52 bytes; tails are ignored.
- Accept session/commands from the configured server IPv4; its source UDP port may differ from the handshake destination. Each queued reply retains its own request source endpoint. Other IPs are ignored.
- After the session reply, VER and measurements are handled in the same command loop, as in the emulator; there is no mandatory VER-first order or 3-second VER deadline. The legacy `--ver-timeout` option is accepted but unused. Default version is `1.1.0.0`, matching AssemblyVersion generated by the supplied emulator project (`Version=1.1.0`) and its bundled DLL, which calls Version.ToString(). The emulator's VERSIONING.md describes three digits but does not match this DLL. Set `--software-version` to the actual server version if different. Sending VER does not prove server acceptance: this protocol has no final acknowledgement.
- Standard measurement commands lazily open the first available RX frontend if none is active. Defaults: 2 MS/s and 1.75 MHz bandwidth, constrained/quantized by backend descriptors; a discrete bandwidth uses the first advertised option. HackRF/Stub gain defaults are 16 dB LNA + 16 dB VGA. Existing explicit settings are retained; bladeRF retains backend gain defaults.
- Range search uses `max(1000, ceil(span_khz/127))` kHz and always includes both endpoints (one capture for a zero-width interval), at most 128 captures. It returns the strongest sampled window and its tuned center; ties choose the first point. It is a bounded coarse search, not a guarantee of finding a narrow signal. RX remains at the last sampled point.
- Measurement power is real normalized mean IQ power in centi-dBFS. It is not calibrated antenna-input dBm. Standard response layouts cannot carry an error status: hardware/capture errors are logged and no fabricated successful response is sent. The server then times out. Extended command status responses remain available.
- Build without `SCANNER_ENABLE_STUB_SDR` for deployment that must never fall back to simulated data. Hardware was not available for this patch's validation.
- Server port defaults to 2653, as in the emulator. Pass the actual port explicitly if your installation uses another one. UDP NAT/port forwarding still has to allow the server's reply to the advertised local port.

Run the standard-protocol integration check after building:

```sh
python tests/emulator_compat_tests.py --client build/scanner_node
```
