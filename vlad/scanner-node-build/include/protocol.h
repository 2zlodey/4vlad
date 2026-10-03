#ifndef SCANNER_NODE_PROTOCOL_H
#define SCANNER_NODE_PROTOCOL_H

#include "device_config.h"

#include <stddef.h>
#include <stdint.h>

#define SCANNER_HANDSHAKE_SIZE 626u
#define SCANNER_SESSION_REPLY_SIZE 52u
#define SCANNER_VER_REQUEST_SIZE 5u
#define SCANNER_VER_RESPONSE_SIZE 14u
#define SCANNER_VER_COMMAND 0x01u

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

void scanner_encode_handshake(uint8_t output[SCANNER_HANDSHAKE_SIZE],
                              const ScannerHandshakeHeader *header, const ScannerDevice *device);
int scanner_decode_ver_request(const uint8_t *bytes, size_t size, ScannerVerRequest *request);
int scanner_encode_ver_response(uint8_t output[SCANNER_VER_RESPONSE_SIZE], uint32_t request_id,
                                const char *version);

#endif