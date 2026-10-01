#ifndef SCANNER_NODE_RADIO_FRONTEND_H
#define SCANNER_NODE_RADIO_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#define SCANNER_RADIO_MAX_FRONTENDS 8u
#define SCANNER_RADIO_MAX_BANDWIDTH_OPTIONS 16u
#define SCANNER_RADIO_MAX_IQ_PAIRS 4096u
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
    SCANNER_RADIO_BACKEND_HACKRF = 2,
    SCANNER_RADIO_BACKEND_STUB = 3
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
    uint8_t backend_index;
    void *device_handle;
    uint64_t configured_frequency_hz;
    uint8_t frequency_configured;
    uint32_t configured_sample_rate_hz;
    uint8_t sample_rate_configured;
    uint32_t configured_bandwidth_hz;
    uint8_t bandwidth_configured;
    uint8_t configured_lna_gain_db;
    uint8_t configured_vga_gain_db;
    uint8_t lna_gain_configured;
    uint8_t vga_gain_configured;
    char name[64];
} ScannerRadioFrontend;

typedef struct
{
    ScannerRadioFrontend frontends[SCANNER_RADIO_MAX_FRONTENDS];
    size_t count;
    uint8_t active_id;
    int hackrf_initialized;
} ScannerRadioInventory;

typedef enum
{
    SCANNER_RADIO_GAIN_LNA = 0,
    SCANNER_RADIO_GAIN_VGA = 1
} ScannerRadioGainStage;

void scanner_radio_discover(ScannerRadioInventory *inventory);
int scanner_radio_add_stub(ScannerRadioInventory *inventory);
int scanner_radio_select(ScannerRadioInventory *inventory, uint8_t frontend_id);
void scanner_radio_close_all(ScannerRadioInventory *inventory);
const ScannerRadioFrontend *scanner_radio_active(const ScannerRadioInventory *inventory);
int scanner_radio_set_frequency(ScannerRadioInventory *inventory, uint8_t channel, uint64_t frequency_hz,
                                uint64_t *actual_frequency_hz);
int scanner_radio_get_frequency(ScannerRadioInventory *inventory, uint8_t channel, uint64_t *frequency_hz);
int scanner_radio_set_sample_rate(ScannerRadioInventory *inventory, uint8_t channel, uint32_t requested_hz,
                                  uint32_t *actual_hz);
int scanner_radio_get_sample_rate(ScannerRadioInventory *inventory, uint8_t channel, uint32_t *sample_rate_hz);
int scanner_radio_set_bandwidth(ScannerRadioInventory *inventory, uint8_t channel, uint32_t requested_hz,
                                uint32_t *actual_hz);
int scanner_radio_get_bandwidth(ScannerRadioInventory *inventory, uint8_t channel, uint32_t *bandwidth_hz);
int scanner_radio_set_gain_stage(ScannerRadioInventory *inventory, uint8_t channel, ScannerRadioGainStage stage,
                                 uint8_t requested_db, uint8_t *actual_db);
int scanner_radio_get_total_gain(ScannerRadioInventory *inventory, uint8_t channel, int16_t *gain_cdb);
int scanner_radio_capture_iq(ScannerRadioInventory *inventory, uint8_t channel, uint16_t complex_pairs, uint8_t *output,
                             size_t output_capacity, size_t *output_size, uint8_t *sample_format,
                             unsigned int timeout_ms);
/* Capture a bounded I/Q window and delegate its noise-floor estimate to scanner_dsp. */
int scanner_radio_measure_noise_floor(ScannerRadioInventory *inventory, uint8_t channel, uint16_t complex_pairs,
                                      int16_t *noise_floor_cdbfs, unsigned int timeout_ms);
const char *scanner_radio_backend_name(ScannerRadioBackend backend);

#endif