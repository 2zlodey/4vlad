#include "protocol.h"
#include "radio_commands.h"

#include <stdio.h>
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

int main(void)
{
    static const uint8_t expected_unsupported[SCANNER_UNSUPPORTED_RESPONSE_SIZE] = {
        0x21, 0x43, 0x65, 0x87, 0x7f, SCANNER_RADIO_STATUS_UNSUPPORTED
    };
    ScannerDatagram datagram;
    ScannerRadioInventory inventory;
    ScannerRadioWorkerResult result;
    int ok = 1;

    memset(&datagram, 0, sizeof(datagram));
    memset(&inventory, 0, sizeof(inventory));
    memset(&result, 0, sizeof(result));
    datagram.size = SCANNER_RADIO_REQUEST_HEADER_SIZE;
    datagram.payload[0] = 0x21;
    datagram.payload[1] = 0x43;
    datagram.payload[2] = 0x65;
    datagram.payload[3] = 0x87;
    datagram.payload[4] = 0x7f;

    ok &= require_true(scanner_radio_command_handle(NULL, &inventory, &datagram, &result, NULL),
                       "Unknown command handler failed");
    ok &= require_true(result.size == sizeof(expected_unsupported)
                           && memcmp(result.payload, expected_unsupported, sizeof(expected_unsupported)) == 0
                           && !result.fatal && !result.close_after_send,
                       "Unknown command did not return the exact NOT_SUPPORTED frame");

    memset(&result, 0, sizeof(result));
    datagram.size = SCANNER_RADIO_REQUEST_HEADER_SIZE + 1;
    datagram.payload[4] = SCANNER_GET_RADIO_FRONTENDS_COMMAND;
    ok &= require_true(scanner_radio_command_handle(NULL, &inventory, &datagram, &result, NULL),
                       "Malformed known command handler failed");
    ok &= require_true(result.size == 0, "Malformed known command was answered as an unknown opcode");

    return ok ? 0 : 1;
}
