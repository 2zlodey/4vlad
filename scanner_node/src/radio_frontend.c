#include "radio_frontend.h"

#include <stdio.h>
#include <string.h>

#ifdef SCANNER_HAVE_BLADERF
#include <libbladeRF.h>
#endif

#ifdef SCANNER_HAVE_HACKRF
#include <libhackrf/hackrf.h>
#endif

#define SCANNER_COMMON_FREQUENCY_MIN_HZ UINT64_C(70000000)
#define SCANNER_COMMON_FREQUENCY_MAX_HZ UINT64_C(6000000000)

#if defined(SCANNER_HAVE_BLADERF) || defined(SCANNER_HAVE_HACKRF)
static uint64_t range_value_u64(int64_t value, float scale)
{
    if (value <= 0 || scale <= 0.0f)
        return 0;
    return (uint64_t)((double)value / (double)scale);
}

static uint32_t range_value_u32(int64_t value, float scale)
{
    uint64_t converted = range_value_u64(value, scale);
    return converted > UINT32_MAX ? UINT32_MAX : (uint32_t)converted;
}

#ifdef SCANNER_HAVE_BLADERF
static uint32_t bladerf_range_step_u32(const struct bladerf_range *range)
{
    return range_value_u32(range->step, range->scale);
}
#endif

static int16_t range_value_cdb(int64_t value, float scale)
{
    double converted;
    if (scale <= 0.0f)
        return 0;
    converted = (double)value / (double)scale * 100.0;
    if (converted > INT16_MAX)
        converted = INT16_MAX;
    else if (converted < INT16_MIN)
        converted = INT16_MIN;
    return (int16_t)converted;
}
#endif

#ifdef SCANNER_HAVE_BLADERF
static void discover_bladerf(ScannerRadioInventory *inventory)
{
    struct bladerf *device = NULL;
    struct bladerf_devinfo *devices = NULL;
    const struct bladerf_range *frequency = NULL;
    const struct bladerf_range *sample_rate = NULL;
    const struct bladerf_range *bandwidth = NULL;
    const struct bladerf_range *gain = NULL;
    const struct bladerf_gain_modes *gain_modes = NULL;
    ScannerRadioFrontend *frontend;
    int device_count;
    int gain_mode_count;
    int mode_index;

    device_count = bladerf_get_device_list(&devices);
    if (device_count <= 0 || devices == NULL)
        return;

    if (inventory->count >= SCANNER_RADIO_MAX_FRONTENDS || bladerf_open_with_devinfo(&device, &devices[0]) != 0)
    {
        bladerf_free_device_list(devices);
        return;
    }

    if (bladerf_get_frequency_range(device, BLADERF_CHANNEL_RX(0), &frequency) != 0
        || bladerf_get_sample_rate_range(device, BLADERF_CHANNEL_RX(0), &sample_rate) != 0
        || bladerf_get_bandwidth_range(device, BLADERF_CHANNEL_RX(0), &bandwidth) != 0
        || bladerf_get_gain_range(device, BLADERF_CHANNEL_RX(0), &gain) != 0)
    {
        bladerf_close(device);
        bladerf_free_device_list(devices);
        return;
    }

    frontend = &inventory->frontends[inventory->count];
    memset(frontend, 0, sizeof(*frontend));
    frontend->id = (uint8_t)inventory->count;
    frontend->backend = SCANNER_RADIO_BACKEND_BLADERF;
    frontend->rx_channels = (uint8_t)bladerf_get_channel_count(device, BLADERF_RX);
    frontend->tx_channels = (uint8_t)bladerf_get_channel_count(device, BLADERF_TX);
    frontend->flags = SCANNER_RADIO_FLAG_FULL_DUPLEX;
    frontend->capabilities = SCANNER_RADIO_CAP_RX | SCANNER_RADIO_CAP_TUNE | SCANNER_RADIO_CAP_SAMPLE_RATE
                             | SCANNER_RADIO_CAP_BANDWIDTH | SCANNER_RADIO_CAP_GAIN;
    if (frontend->tx_channels > 0)
        frontend->capabilities |= SCANNER_RADIO_CAP_TX;
    frontend->frequency_min_hz = range_value_u64(frequency->min, frequency->scale);
    if (frontend->frequency_min_hz < SCANNER_COMMON_FREQUENCY_MIN_HZ)
        frontend->frequency_min_hz = SCANNER_COMMON_FREQUENCY_MIN_HZ;
    frontend->frequency_max_hz = range_value_u64(frequency->max, frequency->scale);
    if (frontend->frequency_max_hz > SCANNER_COMMON_FREQUENCY_MAX_HZ)
        frontend->frequency_max_hz = SCANNER_COMMON_FREQUENCY_MAX_HZ;
    frontend->frequency_step_hz = bladerf_range_step_u32(frequency);
    frontend->sample_rate_min_hz = range_value_u32(sample_rate->min, sample_rate->scale);
    frontend->sample_rate_max_hz = range_value_u32(sample_rate->max, sample_rate->scale);
    frontend->sample_rate_step_hz = bladerf_range_step_u32(sample_rate);
    frontend->bandwidth_min_hz = range_value_u32(bandwidth->min, bandwidth->scale);
    frontend->bandwidth_max_hz = range_value_u32(bandwidth->max, bandwidth->scale);
    frontend->bandwidth_step_hz = bladerf_range_step_u32(bandwidth);
    frontend->gain_min_cdb = range_value_cdb(gain->min, gain->scale);
    frontend->gain_max_cdb = range_value_cdb(gain->max, gain->scale);
    frontend->gain_step_cdb = range_value_cdb(gain->step, gain->scale);
    frontend->sample_resolution_bits = 12;
    frontend->iq_sample_format = SCANNER_RADIO_IQ_FORMAT_S16;
    gain_mode_count = bladerf_get_gain_modes(device, BLADERF_CHANNEL_RX(0), &gain_modes);
    for (mode_index = 0; mode_index < gain_mode_count && gain_modes != NULL; ++mode_index)
    {
        if (gain_modes[mode_index].mode != BLADERF_GAIN_MGC)
            frontend->agc_modes |= SCANNER_RADIO_AGC_HARDWARE;
    }
    frontend->antenna_paths = frontend->rx_channels;
    snprintf(frontend->name, sizeof(frontend->name), "%s", bladerf_get_board_name(device));
    inventory->count++;

    bladerf_close(device);
    bladerf_free_device_list(devices);
}
#endif

#ifdef SCANNER_HAVE_HACKRF
static void discover_hackrf(ScannerRadioInventory *inventory)
{
    hackrf_device_list_t *devices;
    hackrf_device *device = NULL;
    uint8_t board_id = BOARD_ID_INVALID;
    int initialized = 0;
    int index;

    if (hackrf_init() != HACKRF_SUCCESS)
        return;
    initialized = 1;
    devices = hackrf_device_list();
    if (devices == NULL)
        goto cleanup;

    for (index = 0; index < devices->devicecount && inventory->count < SCANNER_RADIO_MAX_FRONTENDS; ++index)
    {
        ScannerRadioFrontend *frontend;
        if (hackrf_device_list_open(devices, index, &device) != HACKRF_SUCCESS)
            continue;
        if (hackrf_board_id_read(device, &board_id) != HACKRF_SUCCESS)
        {
            hackrf_close(device);
            device = NULL;
            continue;
        }

        frontend = &inventory->frontends[inventory->count];
        memset(frontend, 0, sizeof(*frontend));
        frontend->id = (uint8_t)inventory->count;
        frontend->backend = SCANNER_RADIO_BACKEND_HACKRF;
        frontend->rx_channels = 1;
        frontend->tx_channels = 1;
        frontend->capabilities = SCANNER_RADIO_CAP_RX | SCANNER_RADIO_CAP_TX | SCANNER_RADIO_CAP_TUNE
                                 | SCANNER_RADIO_CAP_SAMPLE_RATE | SCANNER_RADIO_CAP_BANDWIDTH | SCANNER_RADIO_CAP_GAIN;
        frontend->frequency_min_hz = SCANNER_COMMON_FREQUENCY_MIN_HZ;
        frontend->frequency_max_hz = SCANNER_COMMON_FREQUENCY_MAX_HZ;
        frontend->frequency_step_hz = 1;
        frontend->sample_rate_min_hz = UINT32_C(2000000);
        frontend->sample_rate_max_hz = UINT32_C(20000000);
        frontend->sample_rate_step_hz = 0;
        frontend->bandwidth_min_hz = UINT32_C(1750000);
        frontend->bandwidth_max_hz = UINT32_C(28000000);
        frontend->bandwidth_step_hz = 0;
        frontend->gain_min_cdb = 0;
        frontend->gain_max_cdb = 11300;
        frontend->gain_step_cdb = 0;
        frontend->sample_resolution_bits = 8;
        frontend->iq_sample_format = SCANNER_RADIO_IQ_FORMAT_S8;
        frontend->bandwidth_option_count = SCANNER_RADIO_MAX_BANDWIDTH_OPTIONS;
        {
            static const uint32_t bandwidth_options[SCANNER_RADIO_MAX_BANDWIDTH_OPTIONS] = {
                1750000, 2500000,  3500000,  5000000,  5500000,  6000000,  7000000,  8000000,
                9000000, 10000000, 12000000, 14000000, 15000000, 20000000, 24000000, 28000000
            };
            memcpy(frontend->bandwidth_options, bandwidth_options, sizeof(bandwidth_options));
        }
        frontend->antenna_paths = 1;
        snprintf(frontend->name, sizeof(frontend->name), "%s", hackrf_board_id_name((enum hackrf_board_id)board_id));
        inventory->count++;
        hackrf_close(device);
        device = NULL;
    }
    hackrf_device_list_free(devices);

cleanup:
    if (device != NULL)
        hackrf_close(device);
    if (initialized)
        hackrf_exit();
}
#endif

void scanner_radio_discover(ScannerRadioInventory *inventory)
{
    if (inventory == NULL)
        return;

    memset(inventory, 0, sizeof(*inventory));
    inventory->active_id = SCANNER_RADIO_ID_NONE;
#ifdef SCANNER_HAVE_BLADERF
    discover_bladerf(inventory);
#endif
#ifdef SCANNER_HAVE_HACKRF
    discover_hackrf(inventory);
#endif
}

int scanner_radio_select(ScannerRadioInventory *inventory, uint8_t frontend_id)
{
    size_t index;
    if (inventory == NULL)
        return 0;
    for (index = 0; index < inventory->count; ++index)
    {
        if (inventory->frontends[index].id == frontend_id)
        {
            inventory->active_id = frontend_id;
            return 1;
        }
    }
    return 0;
}

const ScannerRadioFrontend *scanner_radio_active(const ScannerRadioInventory *inventory)
{
    size_t index;
    if (inventory == NULL || inventory->active_id == SCANNER_RADIO_ID_NONE)
        return NULL;
    for (index = 0; index < inventory->count; ++index)
    {
        if (inventory->frontends[index].id == inventory->active_id)
            return &inventory->frontends[index];
    }
    return NULL;
}

const char *scanner_radio_backend_name(ScannerRadioBackend backend)
{
    switch (backend)
    {
    case SCANNER_RADIO_BACKEND_BLADERF:
        return "BladeRF";
    case SCANNER_RADIO_BACKEND_HACKRF:
        return "HackRF";
    default:
        return "Unknown";
    }
}