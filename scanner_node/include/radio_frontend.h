#ifndef SCANNER_NODE_RADIO_FRONTEND_H
#define SCANNER_NODE_RADIO_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#define SCANNER_RADIO_MAX_FRONTENDS 8u
#define SCANNER_RADIO_MAX_BANDWIDTH_OPTIONS 16u
#define SCANNER_RADIO_ID_NONE 0xffu

#define SCANNER_RADIO_CAP_RX 0x00000001u
#define SCANNER_RADIO_CAP_TX 0x00000002u
#define SCANNER_RADIO_CAP_TUNE 0x00000004u
#define SCANNER_RADIO_CAP_SAMPLE_RATE 0x00000008u
#define SCANNER_RADIO_CAP_BANDWIDTH 0x00000010u
#define SCANNER_RADIO_CAP_GAIN 0x00000020u
#define SCANNER_RADIO_FLAG_FULL_DUPLEX 0x01u
#define SCANNER_RADIO_AGC_HARDWARE 0x01u
#define SCANNER_RADIO_AGC_SOFTWARE 0x02u

#define SCANNER_RADIO_IQ_FORMAT_S8 1u
#define SCANNER_RADIO_IQ_FORMAT_S16 2u

typedef enum
{
    SCANNER_RADIO_BACKEND_UNKNOWN = 0,
    SCANNER_RADIO_BACKEND_BLADERF = 1,
    SCANNER_RADIO_BACKEND_HACKRF = 2
} ScannerRadioBackend;

typedef struct
{
    uint8_t id;
    uint8_t rx_channels;
    uint8_t tx_channels;
    uint8_t flags;
    uint32_t capabilities;
    uint64_t frequency_min_hz;
    uint64_t frequency_max_hz;
    uint32_t frequency_step_hz;
    uint32_t sample_rate_min_hz;
    uint32_t sample_rate_max_hz;
    uint32_t sample_rate_step_hz;
    uint32_t bandwidth_min_hz;
    uint32_t bandwidth_max_hz;
    uint32_t bandwidth_step_hz;
    int16_t gain_min_cdb;
    int16_t gain_max_cdb;
    int16_t gain_step_cdb;
    uint8_t sample_resolution_bits;
    uint8_t iq_sample_format;
    uint8_t agc_modes;
    uint8_t bandwidth_option_count;
    uint8_t antenna_paths;
    uint8_t reserved;
    uint32_t bandwidth_options[SCANNER_RADIO_MAX_BANDWIDTH_OPTIONS];
    ScannerRadioBackend backend;
    char name[64];
} ScannerRadioFrontend;

typedef struct
{
    ScannerRadioFrontend frontends[SCANNER_RADIO_MAX_FRONTENDS];
    size_t count;
    uint8_t active_id;
} ScannerRadioInventory;

void scanner_radio_discover(ScannerRadioInventory *inventory);
int scanner_radio_select(ScannerRadioInventory *inventory, uint8_t frontend_id);
const ScannerRadioFrontend *scanner_radio_active(const ScannerRadioInventory *inventory);
const char *scanner_radio_backend_name(ScannerRadioBackend backend);

#endif