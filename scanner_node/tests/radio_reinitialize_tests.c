#include "protocol.h"
#include "radio_frontend.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    ScannerRadioInventory inventory;
    ScannerRadioFrontend *frontend;
    ScannerVerRequest request;
    ScannerCommutatorRequest board_request;
    const uint8_t packet[] = { 0x78, 0x56, 0x34, 0x12, 0x69 };
    const uint8_t invalid_packet[] = { 0x78, 0x56, 0x34, 0x12, 0x69, 0 };
    uint8_t response[6];
    uint8_t iq[128];
    uint8_t format;
    uint8_t gain;
    uint64_t frequency;
    uint32_t actual;
    size_t iq_size;
    if (!scanner_decode_reinitialize_request(packet, sizeof(packet), &request)
        || request.request_id != UINT32_C(0x12345678)
        || scanner_decode_reinitialize_request(invalid_packet, sizeof(invalid_packet), &request)
        || scanner_decode_reinitialize_request(packet, 4, &request)
        || scanner_decode_reinitialize_request(NULL, 5, &request)
        || scanner_decode_reinitialize_request(packet, 5, NULL)
        || scanner_decode_commutator_request(packet, sizeof(packet), &board_request)
        || scanner_encode_reinitialize_response(response, request.request_id, SCANNER_RADIO_STATUS_OK) != 6
        || memcmp(response, packet, 5) != 0 || response[5] != 0)
        return 1;
    memset(&inventory, 0, sizeof(inventory));
    inventory.active_id = SCANNER_RADIO_ID_NONE;
    if (scanner_radio_reinitialize(NULL) || scanner_radio_reinitialize(&inventory))
        return 2;
    if (!scanner_radio_add_stub(&inventory))
        return 3;
    if (!scanner_radio_select(&inventory, 0))
        return 4;
    frontend = &inventory.frontends[0];
    if (!scanner_radio_set_frequency(&inventory, 0, UINT64_C(100000000), &frequency)
        || !scanner_radio_set_sample_rate(&inventory, 0, 2000000, &actual)
        || !scanner_radio_set_bandwidth(&inventory, 0, 1750000, &actual)
        || !scanner_radio_set_gain_stage(&inventory, 0, SCANNER_RADIO_GAIN_LNA, 16, &gain)
        || !scanner_radio_set_gain_stage(&inventory, 0, SCANNER_RADIO_GAIN_VGA, 20, &gain))
        return 5;
    frontend->capture_buffer = malloc(128);
    if (frontend->capture_buffer == NULL)
        return 6;
    if (!scanner_radio_reinitialize(&inventory) || inventory.active_id != 0
        || frontend->device_handle == NULL || frontend->capture_buffer != NULL
        || frontend->frequency_configured || frontend->sample_rate_configured || frontend->bandwidth_configured
        || frontend->lna_gain_configured || frontend->vga_gain_configured
        || frontend->configured_frequency_hz || frontend->configured_sample_rate_hz
        || frontend->configured_bandwidth_hz || frontend->configured_lna_gain_db || frontend->configured_vga_gain_db
        || scanner_radio_get_sample_rate(&inventory, 0, &actual) != -1
        || scanner_radio_capture_iq(&inventory, 0, 64, iq, sizeof(iq), &iq_size, &format, 2000))
        return 7;
    if (!scanner_radio_set_frequency(&inventory, 0, UINT64_C(100000000), &frequency)
        || !scanner_radio_set_sample_rate(&inventory, 0, 2000000, &actual)
        || !scanner_radio_set_bandwidth(&inventory, 0, 1750000, &actual)
        || !scanner_radio_set_gain_stage(&inventory, 0, SCANNER_RADIO_GAIN_LNA, 16, &gain)
        || !scanner_radio_set_gain_stage(&inventory, 0, SCANNER_RADIO_GAIN_VGA, 20, &gain)
        || !scanner_radio_capture_iq(&inventory, 0, 64, iq, sizeof(iq), &iq_size, &format, 2000))
        return 8;
    frontend->backend = SCANNER_RADIO_BACKEND_UNKNOWN;
    if (scanner_radio_reinitialize(&inventory) || inventory.active_id != SCANNER_RADIO_ID_NONE
        || frontend->device_handle != NULL || frontend->capture_buffer != NULL)
        return 9;
    scanner_radio_close_all(&inventory);
    return 0;
}