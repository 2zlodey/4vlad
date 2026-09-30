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
    ScannerVerRequest request;
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

    remove(argv[2]);
    return ok ? 0 : 1;
}