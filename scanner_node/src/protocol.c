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