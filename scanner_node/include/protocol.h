#ifndef SCANNER_NODE_PROTOCOL_H
#define SCANNER_NODE_PROTOCOL_H

#include "device_config.h"
#include "radio_frontend.h"

#include <stddef.h>
#include <stdint.h>

#define SCANNER_HANDSHAKE_SIZE 626u
#define SCANNER_SESSION_REPLY_SIZE 52u
#define SCANNER_VER_REQUEST_SIZE 5u
#define SCANNER_VER_RESPONSE_SIZE 14u

/* Wire fields below are packed in listed order, without C struct padding.
 * Every request starts with request_id:u32, opcode:u8 (5 bytes).
 * Every radio response starts with request_id:u32, opcode:u8, status:u8 (6 bytes).
 * Req/Resp below list fields AFTER those headers. VER has its own response format.
 * All multi-byte integers are little-endian; uN/iN mean unsigned/signed N-bit integers.
 * request_id and opcode are echoed in responses. status uses SCANNER_RADIO_STATUS_*.
 * request_id: 0..4294967295; radio status: 0..9 (see constants below).
 * channel is a zero-based RX channel of the active frontend, not a frontend ID.
 * Valid channel: 0..rx_channels-1; HackRF/Stub: 0, tested bladeRF2: 0..1.
 * All ranges below include both endpoints. Runtime frontend descriptors take precedence
 * over model-specific examples; *_step_hz=0 means no fixed step is advertised.
 * Response values are valid on status OK; malformed requests are ignored.
 */

/* Query node version during session setup.
 * Req: none (5 bytes total). Resp: version payload (14 bytes total), no radio status.
 */
#define SCANNER_VER_COMMAND 0x01u

/* Enumerate available radios and their capabilities; does not select/open a radio.
 * Req: none. Resp: count:u8, active_id:u8, descriptors[count] (128 bytes each).
 * active_id=0xff means no active frontend; total response size is 8 + count*128.
 * count: 0..SCANNER_RADIO_MAX_FRONTENDS (currently 8); IDs: 0..count-1 or 0xff.
 */
#define SCANNER_GET_RADIO_FRONTENDS_COMMAND 0x04u

/* Select/open a frontend; switching closes the previous one, without starting RX/TX.
 * Req: frontend_id:u8 (0xff closes all). Resp: active_id:u8 (7 bytes total).
 * Valid frontend_id: an ID returned by GET_RADIO_FRONTENDS, or 255 (close all).
 * Other IDs return NOT_FOUND; 255 is not an RX channel number.
 * Selecting the already active frontend preserves its open handle and settings.
 */
#define SCANNER_SET_ACTIVE_RADIO_COMMAND 0x05u

/* Acknowledge shutdown, then close devices and terminate the session.
 * Req: none (5 bytes total). Resp: no fields after status (6 bytes total).
 */
#define SCANNER_EXIT_COMMAND 0xdeu

/* Tune RX to a frequency in whole kHz; request and response use DIFFERENT units.
 * Req: channel:u8, frequency_khz:u32. Resp: channel:u8, applied_frequency_hz:u64.
 * The frequency must be within the active frontend's advertised range.
 * frequency_khz: 70000..6000000 (70 MHz..6 GHz), in whole 1 kHz units;
 * also require frequency_min_hz <= frequency_khz*1000 <= frequency_max_hz.
 * Response frequency_hz: within the advertised range (common cap: 70000000..6000000000).
 */
#define SCANNER_SET_FREQUENCY_COMMAND 0x64u

/* Configure sampling rate in samples per second (Hz).
 * Req: channel:u8, sample_rate_hz:u32. Resp: channel:u8, applied_hz:u32.
 * The applied rate may differ from the requested rate due to hardware constraints.
 * sample_rate_hz: sample_rate_min_hz..sample_rate_max_hz from GET_RADIO_FRONTENDS.
 * HackRF/Stub: 2000000..20000000 Hz; tested bladeRF2: 520834..61440000 Hz.
 * The backend validates/quantizes the requested rate; arbitrary steps are not guaranteed.
 */
#define SCANNER_SET_SAMPLE_RATE_COMMAND 0x65u

/* Read current RX frequency in Hz, without tuning or capturing samples.
 * Req: channel:u8. Resp: channel:u8, frequency_hz:u64.
 * BladeRF reads hardware; HackRF returns the last successful SET, or NOT_CONFIGURED.
 * Successful frequency_hz: frequency_min_hz..frequency_max_hz (70 MHz..6 GHz cap).
 */
#define SCANNER_GET_FREQUENCY_COMMAND 0x6au

/* Set low-noise amplifier gain independently, in integer dB.
 * Req: channel:u8, gain_db:u8. Resp: channel:u8, applied_db:u8.
 * Accepted values/steps depend on the backend; missing stage yields UNSUPPORTED.
 * HackRF/Stub gain_db: {0, 8, 16, 24, 32, 40} dB; applied_db has the same limits.
 * BladeRF: request encoding allows 0..255 dB, but the SDK stage limits decide validity.
 * Tested bladeRF2 lacks this stage and returns UNSUPPORTED; total gain is read via GET_GAIN.
 */
#define SCANNER_SET_LNA_GAIN_COMMAND 0x66u

/* Set variable-gain amplifier gain independently, in integer dB.
 * Req: channel:u8, gain_db:u8. Resp: channel:u8, applied_db:u8.
 * Accepted values/steps depend on the backend; missing stage yields UNSUPPORTED.
 * HackRF/Stub gain_db: 0..62 dB, step 2 dB; applied_db has the same limits.
 * BladeRF: request encoding allows 0..255 dB, but the SDK stage limits decide validity.
 * Tested bladeRF2 lacks this stage and returns UNSUPPORTED.
 */
#define SCANNER_SET_VGA_GAIN_COMMAND 0x67u

/* Configure RX filter bandwidth in Hz; advertised discrete options must be respected.
 * Req: channel:u8, bandwidth_hz:u32. Resp: channel:u8, applied_hz:u32.
 * bandwidth_hz: bandwidth_min_hz..bandwidth_max_hz from GET_RADIO_FRONTENDS.
 * If bandwidth_option_count > 0, only exact bandwidth_options[] entries are valid.
 * HackRF options (Hz): 1750000, 2500000, 3500000, 5000000, 5500000, 6000000,
 * 7000000, 8000000, 9000000, 10000000, 12000000, 14000000, 15000000,
 * 20000000, 24000000, 28000000. Stub: 1750000..28000000 Hz.
 * Tested bladeRF2: 200000..56000000 Hz; SDK may quantize the applied bandwidth.
 */
#define SCANNER_SET_BANDWIDTH_COMMAND 0x68u

/* Reinitialize the active SDR by closing and reopening its backend handle.
 * Req: no arguments (5 bytes). Resp: status only after header (6 bytes).
 * Clears capture resources and previous settings; configure RX again before capture.
 * No selected/open SDR: NO_ACTIVE_FRONTEND; reopen failure: HARDWARE_ERROR.
 * The same frontend remains selected on success; this is not a USB/firmware reset.
 */
#define SCANNER_REINITIALIZE_SDR_COMMAND 0x69u

/* External board antenna selection. Req: channel:u8, antenna:u8 (7 bytes).
 * Resp: channel:u8, applied_antenna:u8 (8 bytes). Channel: 0..1, antenna wire capacity: 0..255.
 * Hardware antenna limits are not defined yet. Placeholder returns UNSUPPORTED, applied=0.
 */
#define SCANNER_SELECT_ANTENNA_COMMAND 0xc8u

/* External board gain/attenuator path. Req: channel:u8, value:u8 (7 bytes).
 * Resp: channel:u8, power_cdb:i16 (9 bytes). Channel: 0..1, value wire capacity: 0..255.
 * Value units/hardware limits and measured power reference are not defined yet.
 * Placeholder returns UNSUPPORTED with power=0; this is not a measurement.
 */
#define SCANNER_SET_PATH_COMMAND 0xc9u

/* Read total gain, not individual stages; centi-dB means dB multiplied by 100.
 * Req: channel:u8. Resp: channel:u8, total_gain_cdb:i16 (9 bytes total).
 * BladeRF reads total hardware gain; HackRF/Stub sum their configured LNA and VGA.
 * HackRF/Stub total_gain_cdb: 0..10200, step 200 (RF amplifier gain is not included).
 * Tested bladeRF2: -1500..6000, step 100; use gain_min/max/step_cdb for other models.
 * i16 wire capacity: -32768..32767; this is encoding capacity, not hardware gain range.
 */
#define SCANNER_GET_GAIN_COMMAND 0x6bu

/* Read gain fields in signed integer dB; their meaning depends on the backend.
 * Req: channel:u8 (6 bytes total).
 * Resp: channel:u8, lna_db:i8, vga_db:i8 (9 bytes total; LNA at byte 7, VGA at byte 8).
 * HackRF/Stub return cached successful SET values, not hardware readback;
 * until BOTH stages have been set, status is NOT_CONFIGURED.
 * BladeRF returns total hardware RX gain in lna_db and zero in vga_db;
 * lna_db is NOT a separate LNA stage on BladeRF and can be negative.
 * This does not change GET_GAIN, which continues to return total gain in centi-dB.
 * HackRF/Stub LNA: 0..40, step 8; VGA: 0..62, step 2 (dB).
 * Tested bladeRF2 LNA field: -15..60, step 1 dB; VGA field: always 0.
 * Both i8 fields can encode -128..127; out-of-capacity backend values fail, not wrap.
 */
#define SCANNER_GET_GAIN_STAGES_COMMAND 0x6eu

/* Read configured sampling rate in Hz; no capture is performed.
 * Req: channel:u8. Resp: channel:u8, sample_rate_hz:u32.
 * Before a successful SET_SAMPLE_RATE, status is NOT_CONFIGURED.
 * Successful sample_rate_hz follows the SET_SAMPLE_RATE descriptor/backend limits above.
 */
#define SCANNER_GET_SAMPLE_RATE_COMMAND 0x6cu

/* Capture/analyze at the current frequency without retuning.
 * Req: channel:u8. Resp: channel:u8, power_cdbfs:i16 (dBFS multiplied by 100).
 * The wire value is normalized mean IQ power, not calibrated dBm or PSD floor.
 * power_cdbfs wire capacity: -32768..32767 (-327.68..327.67 dBFS), signed.
 * Requires configured frequency, sample rate and bandwidth; captures 1024 IQ pairs.
 */
#define SCANNER_MEASURE_CURRENT_COMMAND 0x6du

/* Tune to the requested frequency, then capture/analyze; leaves RX tuned there.
 * Req: channel:u8, frequency_khz:u32. Resp: channel:u8, power_cdbfs:i16.
 * Power uses the same centi-dBFS convention as MEASURE_CURRENT.
 * frequency_khz: 70000..6000000 and within frontend limits, as for SET_FREQUENCY.
 * Requires configured sample rate and bandwidth; captures 1024 IQ pairs.
 */
#define SCANNER_MEASURE_FREQUENCY_COMMAND 0x02u

/* Capture/analyze a frequency grid from start through stop, with nonzero step.
 * Req: channel:u8, start_khz:u32, stop_khz:u32, step_khz:u32 (18 bytes total).
 * Resp: channel:u8, count:u16, points[count]; point = frequency_khz:u32, power_cdbfs:i16.
 * At most 128 points; stop is included if on the grid. Leaves RX at the last point.
 * start_khz/stop_khz: 70000..6000000 and within frontend limits; start_khz <= stop_khz.
 * step_khz: 1..4294967295; floor((stop_khz-start_khz)/step_khz)+1 must be 1..128.
 * A step greater than the span is allowed and yields one point at start_khz.
 * Requires configured sample rate/bandwidth; captures 4096 IQ pairs per point.
 */
#define SCANNER_SWEEP_COMMAND 0x03u

/* Capture raw interleaved I/Q without DSP; complex_pairs counts I+Q sample pairs.
 * Req: channel:u8, complex_pairs:u16 (1..4096).
 * Resp: channel:u8, format:u8, complex_pairs:u16, IQ payload (10-byte header total).
 * format 1 = signed 8-bit I,Q (2 bytes/pair); 2 = signed 16-bit LE I,Q (4 bytes/pair).
 * IQ payload: 2..8192 bytes for format 1, 4..16384 bytes for format 2.
 * Successful format: 1 or 2; returned pair count matches the request.
 * Requires configured frequency, sample rate and bandwidth.
 */
#define SCANNER_GET_RAW_IQ_COMMAND 0x70u

/* Capture raw I/Q into the local --iq-file recording; the network cannot choose a path.
 * Req: channel:u8, complex_pairs:u16 (1..4096).
 * Resp: channel:u8, format:u8, complex_pairs:u16, payload_bytes:u32 (14 bytes total).
 * No IQ payload is sent. NOT_CONFIGURED if no file; successful ACK follows file flush.
 * Successful format: 1 or 2; pair count matches the request; payload_bytes is
 * 2..8192 (format 1) or 4..16384 (format 2), always complex_pairs*bytes_per_pair.
 * Requires configured frequency, sample rate and bandwidth, plus an open --iq-file.
 */
#define SCANNER_SAVE_IQ_TO_FILE_COMMAND 0x71u

#define SCANNER_MAX_SWEEP_POINTS 128u
#define SCANNER_MAX_RAW_IQ_PAIRS SCANNER_RADIO_MAX_IQ_PAIRS
#define SCANNER_RAW_IQ_RESPONSE_HEADER_SIZE 10u
#define SCANNER_SWEEP_RESPONSE_HEADER_SIZE 9u
#define SCANNER_RADIO_COMMAND_RESPONSE_MAX_SIZE (SCANNER_RAW_IQ_RESPONSE_HEADER_SIZE + SCANNER_MAX_RAW_IQ_PAIRS * 4u)

#define SCANNER_RADIO_STATUS_OK 0u
#define SCANNER_RADIO_STATUS_INVALID 1u
#define SCANNER_RADIO_STATUS_NOT_FOUND 2u
#define SCANNER_RADIO_STATUS_NOT_CONFIGURED 3u
#define SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND 4u
#define SCANNER_RADIO_STATUS_INVALID_CHANNEL 5u
#define SCANNER_RADIO_STATUS_OUT_OF_RANGE 6u
#define SCANNER_RADIO_STATUS_HARDWARE_ERROR 7u
#define SCANNER_RADIO_STATUS_UNSUPPORTED 8u
#define SCANNER_RADIO_STATUS_CAPTURE_ERROR 9u

#define SCANNER_RADIO_CAPABILITY_WIRE_SIZE 128u
#define SCANNER_RADIO_FRONTENDS_RESPONSE_MAX_SIZE                                                                      \
    (8u + SCANNER_RADIO_MAX_FRONTENDS * SCANNER_RADIO_CAPABILITY_WIRE_SIZE)

typedef struct
{
    uint32_t counter;
    int64_t seconds;
    int64_t microseconds;
    uint16_t reply_port;
} ScannerHandshakeHeader;

typedef struct
{
    uint32_t request_id;
} ScannerVerRequest;

typedef struct
{
    uint32_t request_id;
} ScannerRadioFrontendsRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t frontend_id;
} ScannerSetActiveRadioRequest;

typedef struct
{
    uint32_t request_id;
} ScannerExitRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
    uint32_t frequency_khz;
} ScannerSetFrequencyRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
} ScannerGetFrequencyRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
    uint32_t value;
} ScannerSetU32Request;

typedef struct
{
    uint32_t request_id;
    uint8_t command;
    uint8_t channel;
    uint8_t value;
} ScannerCommutatorRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
} ScannerGetValueRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
    uint8_t gain_db;
} ScannerSetGainRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
    uint32_t frequency_khz;
} ScannerMeasureFrequencyRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
    uint32_t start_khz;
    uint32_t stop_khz;
    uint32_t step_khz;
} ScannerSweepRequest;

typedef struct
{
    uint32_t request_id;
    uint8_t channel;
    uint16_t complex_pairs;
} ScannerRawIqRequest;

void scanner_encode_handshake(uint8_t output[SCANNER_HANDSHAKE_SIZE], const ScannerHandshakeHeader *header,
                              const ScannerDevice *device);
int scanner_decode_ver_request(const uint8_t *bytes, size_t size, ScannerVerRequest *request);
int scanner_encode_ver_response(uint8_t output[SCANNER_VER_RESPONSE_SIZE], uint32_t request_id, const char *version);
int scanner_decode_radio_frontends_request(const uint8_t *bytes, size_t size, ScannerRadioFrontendsRequest *request);
int scanner_decode_set_active_radio_request(const uint8_t *bytes, size_t size, ScannerSetActiveRadioRequest *request);
int scanner_decode_exit_request(const uint8_t *bytes, size_t size, ScannerExitRequest *request);
int scanner_decode_reinitialize_request(const uint8_t *bytes, size_t size, ScannerVerRequest *request);
size_t scanner_encode_reinitialize_response(uint8_t output[6], uint32_t request_id, uint8_t status);
int scanner_decode_commutator_request(const uint8_t *bytes, size_t size, ScannerCommutatorRequest *request);
size_t scanner_encode_commutator_response(uint8_t output[9], const ScannerCommutatorRequest *request, uint8_t status,
                                          uint8_t applied_antenna, int16_t power_cdb);
int scanner_decode_set_frequency_request(const uint8_t *bytes, size_t size, ScannerSetFrequencyRequest *request);
int scanner_decode_get_frequency_request(const uint8_t *bytes, size_t size, ScannerGetFrequencyRequest *request);
int scanner_decode_set_sample_rate_request(const uint8_t *bytes, size_t size, ScannerSetU32Request *request);
int scanner_decode_get_sample_rate_request(const uint8_t *bytes, size_t size, ScannerGetValueRequest *request);
int scanner_decode_set_bandwidth_request(const uint8_t *bytes, size_t size, ScannerSetU32Request *request);
int scanner_decode_set_gain_request(const uint8_t *bytes, size_t size, uint8_t opcode, ScannerSetGainRequest *request);
int scanner_decode_get_gain_request(const uint8_t *bytes, size_t size, ScannerGetValueRequest *request);
int scanner_decode_get_gain_stages_request(const uint8_t *bytes, size_t size, ScannerGetValueRequest *request);
int scanner_decode_measure_current_request(const uint8_t *bytes, size_t size, ScannerGetValueRequest *request);
int scanner_decode_measure_frequency_request(const uint8_t *bytes, size_t size,
                                             ScannerMeasureFrequencyRequest *request);
int scanner_decode_sweep_request(const uint8_t *bytes, size_t size, ScannerSweepRequest *request);
int scanner_decode_raw_iq_request(const uint8_t *bytes, size_t size, ScannerRawIqRequest *request);
int scanner_decode_save_iq_request(const uint8_t *bytes, size_t size, ScannerRawIqRequest *request);

size_t scanner_encode_radio_frontends_response(uint8_t *output, size_t output_capacity, uint32_t request_id,
                                               const ScannerRadioInventory *inventory);
size_t scanner_encode_set_active_radio_response(uint8_t output[7], uint32_t request_id, uint8_t status,
                                                uint8_t active_id);
size_t scanner_encode_exit_response(uint8_t output[6], uint32_t request_id, uint8_t status);
size_t scanner_encode_frequency_response(uint8_t output[15], uint32_t request_id, uint8_t command, uint8_t status,
                                         uint8_t channel, uint64_t frequency_hz);
size_t scanner_encode_u32_setting_response(uint8_t output[11], uint32_t request_id, uint8_t command, uint8_t status,
                                           uint8_t channel, uint32_t value);
size_t scanner_encode_gain_stage_response(uint8_t output[8], uint32_t request_id, uint8_t command, uint8_t status,
                                          uint8_t channel, uint8_t gain_db);
size_t scanner_encode_gain_response(uint8_t output[9], uint32_t request_id, uint8_t status, uint8_t channel,
                                    int16_t gain_cdb);
size_t scanner_encode_gain_stages_response(uint8_t output[9], uint32_t request_id, uint8_t status, uint8_t channel,
                                           int8_t lna_db, int8_t vga_db);
size_t scanner_encode_power_response(uint8_t output[9], uint32_t request_id, uint8_t command, uint8_t status,
                                     uint8_t channel, int16_t noise_floor_cdbfs);
size_t scanner_encode_raw_iq_response(uint8_t *output, size_t capacity, uint32_t request_id, uint8_t status,
                                      uint8_t channel, uint8_t format, uint16_t complex_pairs, const uint8_t *iq,
                                      size_t iq_size);
size_t scanner_encode_iq_file_response(uint8_t output[14], uint32_t request_id, uint8_t status, uint8_t channel,
                                       uint8_t format, uint16_t complex_pairs, uint32_t payload_size);
size_t scanner_encode_sweep_response(uint8_t *output, size_t capacity, uint32_t request_id, uint8_t status,
                                     uint8_t channel, uint16_t count, const uint32_t *frequency_khz,
                                     const int16_t *noise_floor_cdbfs);

#endif