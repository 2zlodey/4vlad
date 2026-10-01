#include "device_config.h"
#include "protocol.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int require_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    ScannerDevice device;
    ScannerDevice roundtrip;
    ScannerHandshakeHeader header;
    uint8_t handshake[SCANNER_HANDSHAKE_SIZE];
    uint8_t response[SCANNER_VER_RESPONSE_SIZE];
    const uint8_t request_bytes[SCANNER_VER_REQUEST_SIZE] = { 0x78, 0x56, 0x34, 0x12, 0x01 };
    const uint8_t frontends_request_bytes[SCANNER_VER_REQUEST_SIZE] = { 0x78, 0x56, 0x34, 0x12, 0x04 };
    const uint8_t exit_request_bytes[SCANNER_VER_REQUEST_SIZE] = { 0x0b, 0x00, 0x00, 0x00, 0x06 };
    const uint8_t set_frequency_request_bytes[10] = { 0xdd, 0xcc, 0xbb, 0xaa, SCANNER_SET_FREQUENCY_COMMAND,
                                                      1,    0xa0, 0x86, 0x01, 0x00 };
    const uint8_t get_frequency_request_bytes[6] = { 0x0d, 0x00, 0x00, 0x00, SCANNER_GET_FREQUENCY_COMMAND, 1 };
    const uint8_t select_request_bytes[SCANNER_VER_REQUEST_SIZE + 1] = { 0x09, 0x00, 0x00, 0x00, 0x05, 0x00 };
    uint8_t frontends_response[SCANNER_RADIO_FRONTENDS_RESPONSE_MAX_SIZE];
    uint8_t select_response[7];
    uint8_t exit_response[6];
    uint8_t frequency_response[15];
    ScannerVerRequest request;
    ScannerRadioFrontendsRequest frontends_request;
    ScannerSetActiveRadioRequest select_request;
    ScannerExitRequest exit_request;
    ScannerSetFrequencyRequest set_frequency_request;
    ScannerGetFrequencyRequest get_frequency_request;
    ScannerRadioInventory inventory;
    ScannerRadioInventory close_inventory;
#ifdef SCANNER_ENABLE_STUB_SDR
    static const uint8_t expected_stub_iq_cycle[8] = { 0x20, 0x20, 0xe0, 0x20, 0xe0, 0xe0, 0x20, 0xe0 };
    ScannerRadioInventory stub_inventory;
    uint64_t actual_frequency_hz;
    uint64_t readback_frequency_hz;
    uint8_t iq_first[128];
    uint8_t iq_repeat[128];
    uint8_t iq_long[256];
    uint8_t iq_format;
    size_t iq_size;
    int16_t power_first;
    int16_t power_repeat;
    int16_t power_other_frequency;
#endif
    size_t frontends_response_size;
    char error[256];
    size_t index;
    int ok = 1;

    if (argc != 3)
    {
        fprintf(stderr, "protocol_tests requires input JSON and output JSON paths\n");
        return 2;
    }
    ok &= require_true(device_load_json(argv[1], &device, error, sizeof(error)), error);
    if (!ok)
    {
        return 1;
    }
    ok &= require_true(device_save_json(argv[2], &device, error, sizeof(error)), error);
    ok &= require_true(device_load_json(argv[2], &roundtrip, error, sizeof(error)), error);
    ok &= require_true(device.id == roundtrip.id && fabs(device.latitude - roundtrip.latitude) < 1e-9
                           && device.antennas[15].input == roundtrip.antennas[15].input,
                       "JSON save/load roundtrip changed device data");

    memset(&header, 0, sizeof(header));
    header.counter = 0x01020304;
    header.seconds = INT64_C(0x0102030405060708);
    header.microseconds = 123456;
    header.reply_port = 3333;
    scanner_encode_handshake(handshake, &header, &device);
    ok &= require_true(handshake[0] == 0x04 && handshake[3] == 0x01, "Handshake counter is not little-endian");
    ok &= require_true(handshake[20] == 0x05 && handshake[21] == 0x0d, "Handshake reply port is not little-endian");
    ok &= require_true(handshake[22] == (uint8_t)(device.id & 0xff), "Device ID offset or byte order is incorrect");
    ok &= require_true(scanner_decode_ver_request(request_bytes, sizeof(request_bytes), &request),
                       "Valid VER request was rejected");
    ok &= require_true(request.request_id == UINT32_C(0x12345678), "VER RequestId decode failed");
    ok &= require_true(!scanner_decode_ver_request(request_bytes, 4, &request), "Short VER request was accepted");
    ok &= require_true(scanner_encode_ver_response(response, request.request_id, "1.0.0.0"),
                       "VER response encoding failed");
    ok &= require_true(response[0] == 0x78 && response[3] == 0x12, "VER RequestId response is not little-endian");
    ok &= require_true(memcmp(response + 4, "1.0.0.0", 7) == 0, "VER version bytes are incorrect");
    for (index = 11; index < sizeof(response); ++index)
    {
        ok &= require_true(response[index] == 0, "VER version is not zero-padded");
    }
    ok &= require_true(!scanner_encode_ver_response(response, 1, "12345678901"), "Overlong VER version was accepted");

    memset(&inventory, 0, sizeof(inventory));
    inventory.active_id = SCANNER_RADIO_ID_NONE;
    inventory.count = 1;
    inventory.frontends[0].id = 0;
    inventory.frontends[0].rx_channels = 2;
    inventory.frontends[0].tx_channels = 2;
    inventory.frontends[0].capabilities = SCANNER_RADIO_CAP_RX | SCANNER_RADIO_CAP_TX | SCANNER_RADIO_CAP_TUNE;
    inventory.frontends[0].frequency_min_hz = UINT64_C(70000000);
    inventory.frontends[0].frequency_max_hz = UINT64_C(6000000000);
    inventory.frontends[0].frequency_step_hz = 1;
    inventory.frontends[0].sample_rate_min_hz = UINT32_C(2000000);
    inventory.frontends[0].sample_rate_max_hz = UINT32_C(20000000);
    inventory.frontends[0].bandwidth_option_count = 2;
    inventory.frontends[0].bandwidth_options[0] = UINT32_C(1750000);
    inventory.frontends[0].bandwidth_options[1] = UINT32_C(2500000);
    inventory.frontends[0].gain_min_cdb = -250;
    inventory.frontends[0].gain_max_cdb = 6000;
    inventory.frontends[0].sample_resolution_bits = 12;
    inventory.frontends[0].iq_sample_format = SCANNER_RADIO_IQ_FORMAT_S16;
    inventory.frontends[0].agc_modes = SCANNER_RADIO_AGC_HARDWARE;
    ok &= require_true(scanner_decode_radio_frontends_request(frontends_request_bytes, sizeof(frontends_request_bytes),
                                                              &frontends_request),
                       "Valid radio frontend query was rejected");
    ok &= require_true(frontends_request.request_id == UINT32_C(0x12345678),
                       "Radio frontend query RequestId decode failed");
    ok &= require_true(!scanner_decode_radio_frontends_request(request_bytes, sizeof(request_bytes),
                                                               &frontends_request),
                       "VER request was accepted as a frontend query");
    ok &= require_true(scanner_decode_set_active_radio_request(select_request_bytes, sizeof(select_request_bytes),
                                                               &select_request),
                       "Valid active radio selection was rejected");
    ok &= require_true(select_request.request_id == 9 && select_request.frontend_id == 0,
                       "Active radio selection fields were decoded incorrectly");
    ok &= require_true(!scanner_decode_set_active_radio_request(select_request_bytes, sizeof(select_request_bytes) - 1,
                                                                &select_request),
                       "Short active radio selection was accepted");
    ok &= require_true(scanner_decode_exit_request(exit_request_bytes, sizeof(exit_request_bytes), &exit_request)
                           && exit_request.request_id == 11,
                       "Valid Exit request was rejected or decoded incorrectly");
    ok &= require_true(!scanner_decode_exit_request(frontends_request_bytes, sizeof(frontends_request_bytes),
                                                    &exit_request),
                       "Frontend query was accepted as an Exit request");
    ok &= require_true(scanner_decode_set_frequency_request(set_frequency_request_bytes,
                                                            sizeof(set_frequency_request_bytes), &set_frequency_request)
                           && set_frequency_request.request_id == UINT32_C(0xaabbccdd)
                           && set_frequency_request.channel == 1 && set_frequency_request.frequency_khz == 100000,
                       "SET_FREQUENCY request decode failed");
    ok &= require_true(!scanner_decode_set_frequency_request(set_frequency_request_bytes,
                                                             sizeof(set_frequency_request_bytes) - 1,
                                                             &set_frequency_request),
                       "Short SET_FREQUENCY request was accepted");
    ok &= require_true(scanner_decode_get_frequency_request(get_frequency_request_bytes,
                                                            sizeof(get_frequency_request_bytes), &get_frequency_request)
                           && get_frequency_request.request_id == 13 && get_frequency_request.channel == 1,
                       "GET_FREQUENCY request decode failed");
    frontends_response_size = scanner_encode_radio_frontends_response(frontends_response, sizeof(frontends_response),
                                                                      UINT32_C(0x12345678), &inventory);
    ok &= require_true(frontends_response_size == 8 + SCANNER_RADIO_CAPABILITY_WIRE_SIZE,
                       "Radio capability response has the wrong size");
    ok &= require_true(frontends_response[0] == 0x78 && frontends_response[3] == 0x12
                           && frontends_response[4] == SCANNER_GET_RADIO_FRONTENDS_COMMAND
                           && frontends_response[5] == SCANNER_RADIO_STATUS_OK && frontends_response[6] == 1
                           && frontends_response[7] == SCANNER_RADIO_ID_NONE,
                       "Radio capability response header is incorrect");
    ok &= require_true(frontends_response[16] == 0x80 && frontends_response[17] == 0x1d
                           && frontends_response[24] == 0x00 && frontends_response[25] == 0xbc,
                       "Radio frontend frequency limits are not encoded little-endian");
    ok &= require_true(frontends_response[32] == 1 && frontends_response[33] == 0,
                       "Radio frontend frequency step is incorrect");
    ok &= require_true(frontends_response[60] == 0x06 && frontends_response[61] == 0xff,
                       "Signed centi-dB gain is not encoded little-endian");
    ok &= require_true(frontends_response[66] == 12 && frontends_response[67] == SCANNER_RADIO_IQ_FORMAT_S16
                           && frontends_response[68] == SCANNER_RADIO_AGC_HARDWARE && frontends_response[69] == 2,
                       "IQ resolution, format, AGC, or bandwidth option count is incorrect");
    ok &= require_true(frontends_response[72] == 0xf0 && frontends_response[73] == 0xb3
                           && frontends_response[76] == 0xa0 && frontends_response[77] == 0x25,
                       "Discrete bandwidth options are not encoded little-endian");
    ok &= require_true(scanner_encode_set_active_radio_response(select_response, 9, SCANNER_RADIO_STATUS_OK, 0)
                               == sizeof(select_response)
                           && select_response[4] == SCANNER_SET_ACTIVE_RADIO_COMMAND
                           && select_response[5] == SCANNER_RADIO_STATUS_OK && select_response[6] == 0,
                       "Active radio response encoding failed");
    ok &= require_true(scanner_encode_exit_response(exit_response, 11, SCANNER_RADIO_STATUS_OK) == sizeof(exit_response)
                           && exit_response[0] == 11 && exit_response[4] == SCANNER_EXIT_COMMAND
                           && exit_response[5] == SCANNER_RADIO_STATUS_OK,
                       "Exit acknowledgment encoding failed");
    ok &= require_true(scanner_encode_frequency_response(frequency_response, 14, SCANNER_GET_FREQUENCY_COMMAND,
                                                         SCANNER_RADIO_STATUS_OK, 1, UINT64_C(100000000))
                               == sizeof(frequency_response)
                           && frequency_response[0] == 14 && frequency_response[4] == SCANNER_GET_FREQUENCY_COMMAND
                           && frequency_response[5] == SCANNER_RADIO_STATUS_OK && frequency_response[6] == 1
                           && frequency_response[7] == 0x00 && frequency_response[8] == 0xe1
                           && frequency_response[9] == 0xf5 && frequency_response[10] == 0x05
                           && frequency_response[11] == 0x00,
                       "Frequency response does not contain exact little-endian Hz");
    ok &= require_true(scanner_radio_select(&inventory, 0) && scanner_radio_active(&inventory) != NULL,
                       "Available frontend could not be selected");
    ok &= require_true(!scanner_radio_select(&inventory, 1) && scanner_radio_active(&inventory) != NULL,
                       "Unavailable frontend selection was accepted");
    ok &= require_true(scanner_radio_select(&inventory, SCANNER_RADIO_ID_NONE)
                           && scanner_radio_active(&inventory) == NULL,
                       "Neutral frontend selection did not clear the active radio");

    memset(&close_inventory, 0, sizeof(close_inventory));
    close_inventory.count = 1;
    close_inventory.active_id = 0;
    close_inventory.frontends[0].id = 0;
    close_inventory.frontends[0].backend = SCANNER_RADIO_BACKEND_UNKNOWN;
    close_inventory.frontends[0].device_handle = &close_inventory.frontends[0];
    close_inventory.frontends[0].frequency_configured = 1;
    close_inventory.frontends[0].sample_rate_configured = 1;
    close_inventory.frontends[0].bandwidth_configured = 1;
    close_inventory.frontends[0].lna_gain_configured = 1;
    close_inventory.frontends[0].vga_gain_configured = 1;
    scanner_radio_close_all(&close_inventory);
    ok &= require_true(close_inventory.frontends[0].device_handle == NULL
                           && !close_inventory.frontends[0].frequency_configured
                           && !close_inventory.frontends[0].sample_rate_configured
                           && !close_inventory.frontends[0].bandwidth_configured
                           && !close_inventory.frontends[0].lna_gain_configured
                           && !close_inventory.frontends[0].vga_gain_configured,
                       "Closing a radio left cached hardware settings marked configured");

#ifdef SCANNER_ENABLE_STUB_SDR
    memset(&stub_inventory, 0, sizeof(stub_inventory));
    stub_inventory.active_id = SCANNER_RADIO_ID_NONE;
    ok &= require_true(scanner_radio_add_stub(&stub_inventory), "Stub SDR creation failed");
    ok &= require_true(stub_inventory.count == 1 && stub_inventory.frontends[0].rx_channels == 1
                           && stub_inventory.frontends[0].tx_channels == 0
                           && stub_inventory.frontends[0].capabilities
                                  == (SCANNER_RADIO_CAP_RX | SCANNER_RADIO_CAP_TUNE | SCANNER_RADIO_CAP_SAMPLE_RATE
                                      | SCANNER_RADIO_CAP_BANDWIDTH | SCANNER_RADIO_CAP_GAIN),
                       "Stub SDR advertised unexpected capabilities");
    ok &= require_true(scanner_radio_select(&stub_inventory, 0), "Stub SDR selection failed");
    ok &= require_true(scanner_radio_set_frequency(&stub_inventory, 0, UINT64_C(100000000), &actual_frequency_hz)
                           && actual_frequency_hz == UINT64_C(100000000),
                       "Stub SDR frequency set did not return the requested frequency");
    ok &= require_true(scanner_radio_get_frequency(&stub_inventory, 0, &readback_frequency_hz)
                           && readback_frequency_hz == UINT64_C(100000000),
                       "Stub SDR frequency readback failed");
    ok &= require_true(scanner_radio_capture_iq(&stub_inventory, 0, 64, iq_first, sizeof(iq_first), &iq_size,
                                                &iq_format, 100)
                           && iq_size == sizeof(iq_first) && iq_format == SCANNER_RADIO_IQ_FORMAT_S8,
                       "Stub SDR raw IQ capture failed");
    ok &= require_true(memcmp(iq_first, expected_stub_iq_cycle, sizeof(expected_stub_iq_cycle)) == 0,
                       "Stub SDR did not start with its fixed I/Q cycle");
    ok &= require_true(scanner_radio_capture_iq(&stub_inventory, 0, 64, iq_repeat, sizeof(iq_repeat), &iq_size,
                                                &iq_format, 100)
                           && memcmp(iq_first, iq_repeat, sizeof(iq_first)) == 0,
                       "Stub SDR IQ was not deterministic for an identical configuration");
    ok &= require_true(scanner_radio_measure_power(&stub_inventory, 0, 1024, &power_first, 100)
                           && scanner_radio_measure_power(&stub_inventory, 0, 1024, &power_repeat, 100)
                           && power_first == power_repeat,
                       "Stub SDR power was not deterministic for an identical configuration");
    ok &= require_true(scanner_radio_capture_iq(&stub_inventory, 0, 128, iq_long, sizeof(iq_long), &iq_size, &iq_format,
                                                100)
                           && iq_size == sizeof(iq_long) && memcmp(iq_first, iq_long, sizeof(iq_first)) == 0,
                       "Stub SDR IQ prefix changed when requesting a longer block for power");
    ok &= require_true(scanner_radio_set_frequency(&stub_inventory, 0, UINT64_C(300000000), &actual_frequency_hz)
                           && scanner_radio_capture_iq(&stub_inventory, 0, 64, iq_repeat, sizeof(iq_repeat), &iq_size,
                                                       &iq_format, 100)
                           && memcmp(iq_first, iq_repeat, sizeof(iq_first)) != 0
                           && scanner_radio_measure_power(&stub_inventory, 0, 1024, &power_other_frequency, 100)
                           && power_first != power_other_frequency,
                       "Stub SDR spectrum profile did not change deterministically with frequency");
    ok &= require_true(scanner_radio_set_frequency(&stub_inventory, 0, UINT64_C(100000000), &actual_frequency_hz)
                           && scanner_radio_capture_iq(&stub_inventory, 0, 64, iq_repeat, sizeof(iq_repeat), &iq_size,
                                                       &iq_format, 100)
                           && memcmp(iq_first, iq_repeat, sizeof(iq_first)) == 0,
                       "Stub SDR I/Q was not repeatable after returning to the same frequency");
    scanner_radio_close_all(&stub_inventory);
    ok &= require_true(scanner_radio_active(&stub_inventory) == NULL, "Stub SDR close-all did not clear selection");
#endif

    remove(argv[2]);
    return ok ? 0 : 1;
}