#include "protocol.h"

#include <math.h>
#include <string.h>

typedef char scanner_double_must_be_64_bits[(sizeof(double) == sizeof(uint64_t)) ? 1 : -1];

static void write_le(uint8_t *output, uint64_t value, size_t size)
{
    size_t index;
    for (index = 0; index < size; ++index)
    {
        output[index] = (uint8_t)(value & 0xffu);
        value >>= 8;
    }
}

static uint32_t read_u32_le(const uint8_t *input)
{
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) | ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static void write_double_le(uint8_t *output, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_le(output, bits, sizeof(bits));
}

static void write_i32_le(uint8_t *output, int32_t value) { write_le(output, (uint32_t)value, sizeof(uint32_t)); }

void scanner_encode_handshake(uint8_t output[SCANNER_HANDSHAKE_SIZE], const ScannerHandshakeHeader *header,
                              const ScannerDevice *device)
{
    size_t offset = 22;
    size_t index;

    memset(output, 0, SCANNER_HANDSHAKE_SIZE);
    write_le(output, header->counter, 4);
    write_le(output + 4, (uint64_t)header->seconds, 8);
    write_le(output + 12, (uint64_t)header->microseconds, 8);
    write_le(output + 20, header->reply_port, 2);

    write_i32_le(output + offset, device->id);
    write_double_le(output + offset + 4, device->version);
    write_double_le(output + offset + 12, device->longitude);
    write_double_le(output + offset + 20, device->latitude);
    offset += 28;

    for (index = 0; index < SCANNER_NODE_ANTENNA_COUNT; ++index)
    {
        const ScannerAntenna *antenna = &device->antennas[index];
        write_i32_le(output + offset, antenna->input); // 1-16
        write_i32_le(output + offset + 4, antenna->from);
        write_i32_le(output + offset + 8, antenna->to);
        write_double_le(output + offset + 12, antenna->polar);
        write_i32_le(output + offset + 20, antenna->type);
        write_i32_le(output + offset + 24, antenna->dbi);
        write_double_le(output + offset + 28, antenna->direction);
        offset += 36;
    }
}

int scanner_decode_ver_request(const uint8_t *bytes, size_t size, ScannerVerRequest *request)
{
    if (bytes == NULL || request == NULL || size != SCANNER_VER_REQUEST_SIZE || bytes[4] != SCANNER_VER_COMMAND)
        return 0;

    request->request_id = read_u32_le(bytes);
    return 1;
}

int scanner_decode_radio_frontends_request(const uint8_t *bytes, size_t size, ScannerRadioFrontendsRequest *request)
{
    if (bytes == NULL || request == NULL || size != SCANNER_VER_REQUEST_SIZE
        || bytes[4] != SCANNER_GET_RADIO_FRONTENDS_COMMAND)
        return 0;

    request->request_id = read_u32_le(bytes);
    return 1;
}

int scanner_decode_set_active_radio_request(const uint8_t *bytes, size_t size, ScannerSetActiveRadioRequest *request)
{
    if (bytes == NULL || request == NULL || size != SCANNER_VER_REQUEST_SIZE + 1u
        || bytes[4] != SCANNER_SET_ACTIVE_RADIO_COMMAND)
        return 0;

    request->request_id = read_u32_le(bytes);
    request->frontend_id = bytes[5];
    return 1;
}

int scanner_decode_exit_request(const uint8_t *bytes, size_t size, ScannerExitRequest *request)
{
    if (bytes == NULL || request == NULL || size != SCANNER_VER_REQUEST_SIZE || bytes[4] != SCANNER_EXIT_COMMAND)
        return 0;

    request->request_id = read_u32_le(bytes);
    return 1;
}

int scanner_decode_set_frequency_request(const uint8_t *bytes, size_t size, ScannerSetFrequencyRequest *request)
{
    if (bytes == NULL || request == NULL || size != 10u || bytes[4] != SCANNER_SET_FREQUENCY_COMMAND)
        return 0;

    request->request_id = read_u32_le(bytes);
    request->channel = bytes[5];
    request->frequency_khz = read_u32_le(bytes + 6);
    return 1;
}

int scanner_decode_get_frequency_request(const uint8_t *bytes, size_t size, ScannerGetFrequencyRequest *request)
{
    if (bytes == NULL || request == NULL || size != 6u || bytes[4] != SCANNER_GET_FREQUENCY_COMMAND)
        return 0;

    request->request_id = read_u32_le(bytes);
    request->channel = bytes[5];
    return 1;
}

static int encode_radio_frontend(uint8_t *output, const ScannerRadioFrontend *frontend)
{
    size_t index;
    if (frontend->bandwidth_option_count > SCANNER_RADIO_MAX_BANDWIDTH_OPTIONS)
        return 0;

    memset(output, 0, SCANNER_RADIO_CAPABILITY_WIRE_SIZE);
    write_le(output, frontend->id, 1);
    write_le(output + 1, frontend->rx_channels, 1);
    write_le(output + 2, frontend->tx_channels, 1);
    write_le(output + 3, frontend->flags, 1);
    write_le(output + 4, frontend->capabilities, 4);
    write_le(output + 8, frontend->frequency_min_hz, 8);
    write_le(output + 16, frontend->frequency_max_hz, 8);
    write_le(output + 24, frontend->frequency_step_hz, 4);
    write_le(output + 28, frontend->sample_rate_min_hz, 4);
    write_le(output + 32, frontend->sample_rate_max_hz, 4);
    write_le(output + 36, frontend->sample_rate_step_hz, 4);
    write_le(output + 40, frontend->bandwidth_min_hz, 4);
    write_le(output + 44, frontend->bandwidth_max_hz, 4);
    write_le(output + 48, frontend->bandwidth_step_hz, 4);
    write_le(output + 52, (uint16_t)frontend->gain_min_cdb, 2);
    write_le(output + 54, (uint16_t)frontend->gain_max_cdb, 2);
    write_le(output + 56, (uint16_t)frontend->gain_step_cdb, 2);
    write_le(output + 58, frontend->sample_resolution_bits, 1);
    write_le(output + 59, frontend->iq_sample_format, 1);
    write_le(output + 60, frontend->agc_modes, 1);
    write_le(output + 61, frontend->bandwidth_option_count, 1);
    write_le(output + 62, frontend->antenna_paths, 1);
    write_le(output + 63, frontend->reserved, 1);
    for (index = 0; index < frontend->bandwidth_option_count; ++index)
        write_le(output + 64 + index * 4, frontend->bandwidth_options[index], 4);
    return 1;
}

size_t scanner_encode_radio_frontends_response(uint8_t *output, size_t output_capacity, uint32_t request_id,
                                               const ScannerRadioInventory *inventory)
{
    size_t index;
    size_t response_size;
    size_t offset;

    if (output == NULL || inventory == NULL || inventory->count > SCANNER_RADIO_MAX_FRONTENDS)
        return 0;
    response_size = 8u + inventory->count * SCANNER_RADIO_CAPABILITY_WIRE_SIZE;
    if (output_capacity < response_size)
        return 0;

    write_le(output, request_id, 4);
    output[4] = SCANNER_GET_RADIO_FRONTENDS_COMMAND;
    output[5] = SCANNER_RADIO_STATUS_OK;
    output[6] = (uint8_t)inventory->count;
    output[7] = inventory->active_id;
    offset = 8;
    for (index = 0; index < inventory->count; ++index)
    {
        if (!encode_radio_frontend(output + offset, &inventory->frontends[index]))
            return 0;
        offset += SCANNER_RADIO_CAPABILITY_WIRE_SIZE;
    }
    return response_size;
}

size_t scanner_encode_set_active_radio_response(uint8_t output[7], uint32_t request_id, uint8_t status,
                                                uint8_t active_id)
{
    if (output == NULL)
        return 0;
    write_le(output, request_id, 4);
    output[4] = SCANNER_SET_ACTIVE_RADIO_COMMAND;
    output[5] = status;
    output[6] = active_id;
    return 7;
}

size_t scanner_encode_exit_response(uint8_t output[6], uint32_t request_id, uint8_t status)
{
    if (output == NULL)
        return 0;
    write_le(output, request_id, 4);
    output[4] = SCANNER_EXIT_COMMAND;
    output[5] = status;
    return 6;
}

size_t scanner_encode_frequency_response(uint8_t output[15], uint32_t request_id, uint8_t command, uint8_t status,
                                         uint8_t channel, uint64_t frequency_hz)
{
    if (output == NULL || (command != SCANNER_SET_FREQUENCY_COMMAND && command != SCANNER_GET_FREQUENCY_COMMAND))
        return 0;
    write_le(output, request_id, 4);
    output[4] = command;
    output[5] = status;
    output[6] = channel;
    write_le(output + 7, frequency_hz, 8);
    return 15;
}

int scanner_encode_ver_response(uint8_t output[SCANNER_VER_RESPONSE_SIZE], uint32_t request_id, const char *version)
{
    size_t length;
    size_t index;

    if (output == NULL || version == NULL)
        return 0;

    length = strlen(version);
    if (length == 0 || length > 10)
        return 0;

    for (index = 0; index < length; ++index)
    {
        if ((unsigned char)version[index] > 0x7f)
            return 0;
    }

    memset(output, 0, SCANNER_VER_RESPONSE_SIZE);
    write_le(output, request_id, 4);
    memcpy(output + 4, version, length);
    return 1;
}