# Orkestr Scanner Node

Portable C99 UDP client for the Orkestr handshake, VER exchange, and SDR commands. BladeRF/HackRF backends are optional; the Stub backend supports tests without hardware.

## Wire Protocol

All multibyte fields are little-endian. Radio commands use the common headers below; VER is the exception.

| Frame | Layout | Size |
| --- | --- | ---: |
| Handshake | Device handshake fields | 626 B |
| Session reply | `[key:32][nonce:12][timestamp:8]` | 52 B |
| VER request | `[opcode:0x01]` | 1 B |
| VER response | `[opcode:0x01][version:10 ASCII bytes, zero-padded]` | 11 B |
| Radio request | `[request_id:u32][opcode:u8][arguments]` | varies |
| Radio response | `[request_id:u32][opcode:u8][status:u8][fields]` | varies |

VER does not carry RequestId/status. Radio command arguments keep the units shown below; capability descriptors are in Hz.

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
| `0x02` | `MEASURE_FREQUENCY` | `channel:u8, frequency_khz:u32` | `status:u8, channel:u8, power_cdbfs:i16` |
| `0x03` | `SWEEP` | `channel:u8, start_khz:u32, stop_khz:u32, step_khz:u32` | `status:u8, channel:u8, count:u16, points[{frequency_khz:u32,power_cdbfs:i16}]` |
| `0x70` | `GET_RAW_IQ` | `channel:u8, complex_pairs:u16` | `status:u8, channel:u8, format:u8, complex_pairs:u16, IQ bytes[]` |
| `0x71` | `SAVE_IQ_TO_FILE` | `channel:u8, complex_pairs:u16` | `status:u8, channel:u8, format:u8, complex_pairs:u16, payload_bytes:u32` |
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

`SET_ACTIVE_RADIO` also uses `2` when the frontend ID is absent. `active_id=0xff` means no frontend selected.

## Radio Behavior

| Area | Behavior |
| --- | --- |
| RX channel | Zero-based; supported channels are `0` and `1`. |
| Reinitialize (`0x69`) | Closes/reopens the active backend and clears cached settings. Returns `4` with no active radio, `7` on reopen failure. |
| Commutator (`0xC8/0xC9`) | Placeholder only; logs requests and returns `8`. No hardware operation is performed. |
| Gain | Total gain is signed centi-dB. Gain-stage values are signed dB; BladeRF reports total RX gain in LNA and `0` in VGA. |
| DSP | Welch PSD, Hann window, 50% overlap; FFT sizes 64-4096. Measurement uses 1024 samples; sweep uses 4096. |
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
