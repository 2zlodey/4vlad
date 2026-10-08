#include "commutator.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t commands[] = { 0xc8, 0xc9 };
    const size_t response_sizes[] = { 8, 9 };
    size_t index;
    uint8_t applied = 255;
    int16_t power = 123;
    if (scanner_commutator_select_antenna(1, 255, &applied) != SCANNER_COMMUTATOR_UNSUPPORTED || applied != 0
        || scanner_commutator_set_path(1, 255, &power) != SCANNER_COMMUTATOR_UNSUPPORTED || power != 0)
        return 1;
    for (index = 0; index < sizeof(commands); ++index)
    {
        uint8_t packet[] = { 0x78, 0x56, 0x34, 0x12, commands[index], 1, 255 };
        uint8_t response[9];
        ScannerCommutatorRequest request;
        size_t size = 7;
        if (!scanner_decode_commutator_request(packet, size, &request) || request.request_id != UINT32_C(0x12345678)
            || request.command != commands[index] || request.channel != 1 || request.value != 255)
            return 2;
        memset(response, 0xcc, sizeof(response));
        if (scanner_encode_commutator_response(response, &request, SCANNER_RADIO_STATUS_UNSUPPORTED, 255, -123)
                != response_sizes[index]
            || memcmp(response, packet, 5) != 0 || response[5] != SCANNER_RADIO_STATUS_UNSUPPORTED || response[6] != 1
            || response[7] != 0 || (index == 1 && response[8] != 0))
            return 3;
        if (scanner_encode_commutator_response(response, &request, SCANNER_RADIO_STATUS_OK, 255, -123)
                != response_sizes[index]
            || (index == 0 && response[7] != 255) || (index == 1 && (response[7] != 0x85 || response[8] != 0xff)))
            return 4;
        if (scanner_decode_commutator_request(packet, size - 1, &request)
            || scanner_decode_commutator_request(packet, size + 1, &request))
            return 5;
        packet[4] = SCANNER_EXIT_COMMAND;
        if (scanner_decode_commutator_request(packet, size, &request))
            return 6;
    }
    if (scanner_decode_commutator_request(NULL, 0, NULL) || scanner_encode_commutator_response(NULL, NULL, 0, 0, 0))
        return 7;
    puts("Commutator placeholder and protocol tests passed");
    return 0;
}