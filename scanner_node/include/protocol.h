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
#define SCANNER_VER_COMMAND 0x01u
#define SCANNER_GET_RADIO_FRONTENDS_COMMAND 0x04u
#define SCANNER_SET_ACTIVE_RADIO_COMMAND 0x05u
#define SCANNER_EXIT_COMMAND 0x06u
#define SCANNER_SET_FREQUENCY_COMMAND 0x64u
#define SCANNER_GET_FREQUENCY_COMMAND 0x6au
#define SCANNER_RADIO_STATUS_OK 0u
#define SCANNER_RADIO_STATUS_INVALID 1u
#define SCANNER_RADIO_STATUS_NOT_FOUND 2u
#define SCANNER_RADIO_STATUS_NOT_CONFIGURED 3u
#define SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND 4u
#define SCANNER_RADIO_STATUS_INVALID_CHANNEL 5u
#define SCANNER_RADIO_STATUS_OUT_OF_RANGE 6u
#define SCANNER_RADIO_STATUS_HARDWARE_ERROR 7u
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

void scanner_encode_handshake(uint8_t output[SCANNER_HANDSHAKE_SIZE], const ScannerHandshakeHeader *header,
                              const ScannerDevice *device);
int scanner_decode_ver_request(const uint8_t *bytes, size_t size, ScannerVerRequest *request);
int scanner_encode_ver_response(uint8_t output[SCANNER_VER_RESPONSE_SIZE], uint32_t request_id, const char *version);
int scanner_decode_radio_frontends_request(const uint8_t *bytes, size_t size, ScannerRadioFrontendsRequest *request);
int scanner_decode_set_active_radio_request(const uint8_t *bytes, size_t size, ScannerSetActiveRadioRequest *request);
int scanner_decode_exit_request(const uint8_t *bytes, size_t size, ScannerExitRequest *request);
int scanner_decode_set_frequency_request(const uint8_t *bytes, size_t size, ScannerSetFrequencyRequest *request);
int scanner_decode_get_frequency_request(const uint8_t *bytes, size_t size, ScannerGetFrequencyRequest *request);
size_t scanner_encode_radio_frontends_response(uint8_t *output, size_t output_capacity, uint32_t request_id,
                                               const ScannerRadioInventory *inventory);
size_t scanner_encode_set_active_radio_response(uint8_t output[7], uint32_t request_id, uint8_t status,
                                                uint8_t active_id);
size_t scanner_encode_exit_response(uint8_t output[6], uint32_t request_id, uint8_t status);
size_t scanner_encode_frequency_response(uint8_t output[15], uint32_t request_id, uint8_t command, uint8_t status,
                                         uint8_t channel, uint64_t frequency_hz);

#endif