#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "radio_frontend.h"

#include "dsp.h"
#include "radio_capture_backend.h"

#include <stdio.h>
#include <stdlib.h>
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
    frontend->backend_index = 0;
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
        frontend->backend_index = (uint8_t)index;
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

int scanner_radio_add_stub(ScannerRadioInventory *inventory)
{
#ifdef SCANNER_ENABLE_STUB_SDR
    ScannerRadioFrontend *frontend;

    if (inventory == NULL || inventory->count >= SCANNER_RADIO_MAX_FRONTENDS)
        return 0;

    frontend = &inventory->frontends[inventory->count];
    memset(frontend, 0, sizeof(*frontend));
    frontend->id = (uint8_t)inventory->count;
    frontend->backend = SCANNER_RADIO_BACKEND_STUB;
    frontend->rx_channels = 1;
    frontend->capabilities = SCANNER_RADIO_CAP_RX | SCANNER_RADIO_CAP_TUNE | SCANNER_RADIO_CAP_SAMPLE_RATE
                             | SCANNER_RADIO_CAP_BANDWIDTH | SCANNER_RADIO_CAP_GAIN;
    frontend->frequency_min_hz = SCANNER_COMMON_FREQUENCY_MIN_HZ;
    frontend->frequency_max_hz = SCANNER_COMMON_FREQUENCY_MAX_HZ;
    frontend->frequency_step_hz = 1000;
    frontend->sample_rate_min_hz = UINT32_C(2000000);
    frontend->sample_rate_max_hz = UINT32_C(20000000);
    frontend->sample_rate_step_hz = 1;
    frontend->bandwidth_min_hz = UINT32_C(1750000);
    frontend->bandwidth_max_hz = UINT32_C(28000000);
    frontend->bandwidth_step_hz = 1;
    frontend->gain_min_cdb = 0;
    frontend->gain_max_cdb = 10200;
    frontend->gain_step_cdb = 100;
    frontend->sample_resolution_bits = 8;
    frontend->iq_sample_format = SCANNER_RADIO_IQ_FORMAT_S8;
    frontend->antenna_paths = 1;
    frontend->configured_sample_rate_hz = UINT32_C(2000000);
    frontend->sample_rate_configured = 1;
    frontend->configured_bandwidth_hz = UINT32_C(1750000);
    frontend->bandwidth_configured = 1;
    frontend->configured_lna_gain_db = 0;
    frontend->configured_vga_gain_db = 0;
    frontend->lna_gain_configured = 1;
    frontend->vga_gain_configured = 1;
    snprintf(frontend->name, sizeof(frontend->name), "%s", "Stub SDR (in-memory)");
    inventory->count++;
    return 1;
#else
    (void)inventory;
    return 0;
#endif
}

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
    if (inventory->count == 0)
        (void)scanner_radio_add_stub(inventory);
}

static void release_capture_resources(ScannerRadioFrontend *frontend)
{
    free(frontend->capture_buffer);
    frontend->capture_buffer = NULL;
}

int scanner_radio_select(ScannerRadioInventory *inventory, uint8_t frontend_id)
{
    ScannerRadioFrontend *target = NULL;
    size_t index;
    if (inventory == NULL)
        return 0;

    if (frontend_id == SCANNER_RADIO_ID_NONE)
    {
        scanner_radio_close_all(inventory);
        return 1;
    }

    for (index = 0; index < inventory->count; ++index)
    {
        if (inventory->frontends[index].id == frontend_id)
        {
            target = &inventory->frontends[index];
            break;
        }
    }
    if (target == NULL)
        return 0;
    if (inventory->active_id == frontend_id && target->device_handle != NULL)
        return 1;

    for (index = 0; index < inventory->count; ++index)
    {
        ScannerRadioFrontend *frontend = &inventory->frontends[index];
        release_capture_resources(frontend);
        if (frontend->device_handle == NULL)
            continue;
#ifdef SCANNER_HAVE_BLADERF
        if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
            bladerf_close((struct bladerf *)frontend->device_handle);
#endif
#ifdef SCANNER_HAVE_HACKRF
        if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF)
            hackrf_close((hackrf_device *)frontend->device_handle);
#endif
        frontend->device_handle = NULL;
        frontend->configured_frequency_hz = 0;
        frontend->frequency_configured = 0;
        frontend->sample_rate_configured = frontend->backend == SCANNER_RADIO_BACKEND_STUB;
        frontend->bandwidth_configured = frontend->backend == SCANNER_RADIO_BACKEND_STUB;
        frontend->lna_gain_configured = frontend->backend == SCANNER_RADIO_BACKEND_STUB;
        frontend->vga_gain_configured = frontend->backend == SCANNER_RADIO_BACKEND_STUB;
    }
    inventory->active_id = SCANNER_RADIO_ID_NONE;

    if (target->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
#ifdef SCANNER_HAVE_BLADERF
        struct bladerf *device = NULL;
        struct bladerf_devinfo *devices = NULL;
        int device_count = bladerf_get_device_list(&devices);
        int open_status = -1;
        if (device_count > (int)target->backend_index && devices != NULL)
            open_status = bladerf_open_with_devinfo(&device, &devices[target->backend_index]);
        bladerf_free_device_list(devices);
        if (open_status != 0)
            return 0;
        target->device_handle = device;
        {
            bladerf_frequency frequency;
            if (bladerf_get_frequency(device, BLADERF_CHANNEL_RX(0), &frequency) == 0)
            {
                target->configured_frequency_hz = frequency;
                target->frequency_configured = 1;
            }
        }
#else
        return 0;
#endif
    }
    else if (target->backend == SCANNER_RADIO_BACKEND_HACKRF)
    {
#ifdef SCANNER_HAVE_HACKRF
        hackrf_device_list_t *devices;
        hackrf_device *device = NULL;
        if (!inventory->hackrf_initialized)
        {
            if (hackrf_init() != HACKRF_SUCCESS)
                return 0;
            inventory->hackrf_initialized = 1;
        }
        devices = hackrf_device_list();
        if (devices == NULL)
        {
            hackrf_exit();
            inventory->hackrf_initialized = 0;
            return 0;
        }
        if (devices->devicecount > (int)target->backend_index)
            (void)hackrf_device_list_open(devices, (int)target->backend_index, &device);
        hackrf_device_list_free(devices);
        if (device == NULL)
        {
            if (inventory->hackrf_initialized)
            {
                hackrf_exit();
                inventory->hackrf_initialized = 0;
            }
            return 0;
        }
        target->device_handle = device;
#else
        return 0;
#endif
    }
    else if (target->backend == SCANNER_RADIO_BACKEND_STUB)
    {
        /* A non-null marker makes the stub participate in the normal active-radio path. */
        target->device_handle = target;
        target->configured_frequency_hz = 0;
        target->frequency_configured = 0;
    }
    else if (target->backend == SCANNER_RADIO_BACKEND_UNKNOWN)
    {
        inventory->active_id = frontend_id;
        return 1;
    }
    else
    {
        return 0;
    }

    inventory->active_id = frontend_id;
    return 1;
}

void scanner_radio_close_all(ScannerRadioInventory *inventory)
{
    size_t index;
    if (inventory == NULL)
        return;

    for (index = 0; index < inventory->count; ++index)
    {
        ScannerRadioFrontend *frontend = &inventory->frontends[index];
        release_capture_resources(frontend);
        if (frontend->device_handle == NULL)
            continue;
#ifdef SCANNER_HAVE_BLADERF
        if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
            bladerf_close((struct bladerf *)frontend->device_handle);
#endif
#ifdef SCANNER_HAVE_HACKRF
        if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF)
            hackrf_close((hackrf_device *)frontend->device_handle);
#endif
        frontend->device_handle = NULL;
        frontend->configured_frequency_hz = 0;
        frontend->frequency_configured = 0;
        if (frontend->backend != SCANNER_RADIO_BACKEND_STUB)
        {
            frontend->sample_rate_configured = 0;
            frontend->bandwidth_configured = 0;
            frontend->lna_gain_configured = 0;
            frontend->vga_gain_configured = 0;
        }
    }
#ifdef SCANNER_HAVE_HACKRF
    if (inventory->hackrf_initialized)
    {
        hackrf_exit();
        inventory->hackrf_initialized = 0;
    }
#endif
    inventory->active_id = SCANNER_RADIO_ID_NONE;
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
    case SCANNER_RADIO_BACKEND_STUB:
        return "Stub SDR";
    default:
        return "Unknown";
    }
}

static int set_frequency(ScannerRadioInventory *inventory, uint8_t channel, uint64_t frequency_hz,
                         uint64_t *actual_frequency_hz, int readback)
{
    ScannerRadioFrontend *frontend;
    uint64_t frequency_result = frequency_hz;
    if (inventory == NULL || (readback && actual_frequency_hz == NULL))
        return 0;
    frontend = (ScannerRadioFrontend *)scanner_radio_active(inventory);
    if (frontend == NULL || frontend->device_handle == NULL || channel >= frontend->rx_channels
        || frequency_hz < frontend->frequency_min_hz || frequency_hz > frontend->frequency_max_hz)
        return 0;

    if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
#ifdef SCANNER_HAVE_BLADERF
        struct bladerf *device = (struct bladerf *)frontend->device_handle;
        if (bladerf_set_frequency(device, BLADERF_CHANNEL_RX(channel), frequency_hz) != 0)
            return 0;
        if (readback)
        {
            bladerf_frequency actual;
            if (bladerf_get_frequency(device, BLADERF_CHANNEL_RX(channel), &actual) != 0)
                return 0;
            frequency_result = actual;
        }
#else
        return 0;
#endif
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF)
    {
#ifdef SCANNER_HAVE_HACKRF
        if (channel != 0 || hackrf_set_freq((hackrf_device *)frontend->device_handle, frequency_hz) != HACKRF_SUCCESS)
            return 0;
#else
        return 0;
#endif
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_STUB)
    {
    }
    else
    {
        return 0;
    }

    frontend->configured_frequency_hz = frequency_result;
    frontend->frequency_configured = 1;
    if (readback)
        *actual_frequency_hz = frequency_result;
    return 1;
}

int scanner_radio_set_frequency(ScannerRadioInventory *inventory, uint8_t channel, uint64_t frequency_hz,
                                uint64_t *actual_frequency_hz)
{
    return set_frequency(inventory, channel, frequency_hz, actual_frequency_hz, 1);
}

int scanner_radio_set_frequency_no_readback(ScannerRadioInventory *inventory, uint8_t channel, uint64_t frequency_hz)
{
    return set_frequency(inventory, channel, frequency_hz, NULL, 0);
}

int scanner_radio_get_frequency(ScannerRadioInventory *inventory, uint8_t channel, uint64_t *frequency_hz)
{
    ScannerRadioFrontend *frontend;
    if (inventory == NULL || frequency_hz == NULL)
        return 0;
    frontend = (ScannerRadioFrontend *)scanner_radio_active(inventory);
    if (frontend == NULL || frontend->device_handle == NULL || channel >= frontend->rx_channels)
        return 0;

    if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
#ifdef SCANNER_HAVE_BLADERF
        bladerf_frequency actual;
        if (bladerf_get_frequency((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), &actual) != 0)
            return 0;
        frontend->configured_frequency_hz = actual;
        frontend->frequency_configured = 1;
#else
        return 0;
#endif
    }
    else if (!frontend->frequency_configured)
    {
        return -1;
    }

    *frequency_hz = frontend->configured_frequency_hz;
    return 1;
}

static ScannerRadioFrontend *active_frontend_mutable(ScannerRadioInventory *inventory, uint8_t channel)
{
    ScannerRadioFrontend *frontend;
    if (inventory == NULL)
        return NULL;
    frontend = (ScannerRadioFrontend *)scanner_radio_active(inventory);
    if (frontend == NULL || frontend->device_handle == NULL || channel >= frontend->rx_channels)
        return NULL;
    return frontend;
}

int scanner_radio_set_sample_rate(ScannerRadioInventory *inventory, uint8_t channel, uint32_t requested_hz,
                                  uint32_t *actual_hz)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    if (frontend == NULL || actual_hz == NULL || requested_hz < frontend->sample_rate_min_hz
        || requested_hz > frontend->sample_rate_max_hz)
        return 0;

    if (frontend->backend == SCANNER_RADIO_BACKEND_STUB)
    {
        frontend->configured_sample_rate_hz = requested_hz;
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
#ifdef SCANNER_HAVE_BLADERF
        if (bladerf_set_sample_rate((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel),
                                    requested_hz, actual_hz)
            != 0)
            return 0;
        frontend->configured_sample_rate_hz = *actual_hz;
#else
        return 0;
#endif
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF)
    {
#ifdef SCANNER_HAVE_HACKRF
        if (channel != 0
            || hackrf_set_sample_rate((hackrf_device *)frontend->device_handle, (double)requested_hz) != HACKRF_SUCCESS)
            return 0;
        frontend->configured_sample_rate_hz = requested_hz;
#else
        return 0;
#endif
    }
    else
        return 0;

    frontend->sample_rate_configured = 1;
    *actual_hz = frontend->configured_sample_rate_hz;
    return 1;
}

int scanner_radio_get_sample_rate(ScannerRadioInventory *inventory, uint8_t channel, uint32_t *sample_rate_hz)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    if (frontend == NULL || sample_rate_hz == NULL)
        return 0;
    if (!frontend->sample_rate_configured)
        return -1;
#ifdef SCANNER_HAVE_BLADERF
    if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
        unsigned int actual = 0;
        if (bladerf_get_sample_rate((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), &actual)
            != 0)
            return 0;
        frontend->configured_sample_rate_hz = actual;
    }
#endif
    *sample_rate_hz = frontend->configured_sample_rate_hz;
    return 1;
}

int scanner_radio_set_bandwidth(ScannerRadioInventory *inventory, uint8_t channel, uint32_t requested_hz,
                                uint32_t *actual_hz)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    if (frontend == NULL || actual_hz == NULL || requested_hz < frontend->bandwidth_min_hz
        || requested_hz > frontend->bandwidth_max_hz)
        return 0;
    if (frontend->bandwidth_option_count > 0)
    {
        size_t index;
        int found = 0;
        for (index = 0; index < frontend->bandwidth_option_count; ++index)
        {
            if (frontend->bandwidth_options[index] == requested_hz)
            {
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }

    if (frontend->backend == SCANNER_RADIO_BACKEND_STUB)
    {
        frontend->configured_bandwidth_hz = requested_hz;
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
#ifdef SCANNER_HAVE_BLADERF
        if (bladerf_set_bandwidth((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), requested_hz,
                                  actual_hz)
            != 0)
            return 0;
        frontend->configured_bandwidth_hz = *actual_hz;
#else
        return 0;
#endif
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF)
    {
#ifdef SCANNER_HAVE_HACKRF
        if (channel != 0
            || hackrf_set_baseband_filter_bandwidth((hackrf_device *)frontend->device_handle, requested_hz)
                   != HACKRF_SUCCESS)
            return 0;
        frontend->configured_bandwidth_hz = requested_hz;
#else
        return 0;
#endif
    }
    else
        return 0;

    frontend->bandwidth_configured = 1;
    *actual_hz = frontend->configured_bandwidth_hz;
    return 1;
}

int scanner_radio_get_bandwidth(ScannerRadioInventory *inventory, uint8_t channel, uint32_t *bandwidth_hz)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    if (frontend == NULL || bandwidth_hz == NULL)
        return 0;
    if (!frontend->bandwidth_configured)
        return -1;
#ifdef SCANNER_HAVE_BLADERF
    if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
        unsigned int actual = 0;
        if (bladerf_get_bandwidth((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), &actual) != 0)
            return 0;
        frontend->configured_bandwidth_hz = actual;
    }
#endif
    *bandwidth_hz = frontend->configured_bandwidth_hz;
    return 1;
}

int scanner_radio_set_gain_stage(ScannerRadioInventory *inventory, uint8_t channel, ScannerRadioGainStage stage,
                                 uint8_t requested_db, uint8_t *actual_db)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    uint8_t *stored_gain;
    uint8_t *configured;
    if (frontend == NULL || actual_db == NULL || (stage != SCANNER_RADIO_GAIN_LNA && stage != SCANNER_RADIO_GAIN_VGA))
        return 0;
    stored_gain = stage == SCANNER_RADIO_GAIN_LNA ? &frontend->configured_lna_gain_db
                                                  : &frontend->configured_vga_gain_db;
    configured = stage == SCANNER_RADIO_GAIN_LNA ? &frontend->lna_gain_configured : &frontend->vga_gain_configured;

    if (frontend->backend == SCANNER_RADIO_BACKEND_STUB)
    {
        if ((stage == SCANNER_RADIO_GAIN_LNA && (requested_db > 40 || requested_db % 8 != 0))
            || (stage == SCANNER_RADIO_GAIN_VGA && (requested_db > 62 || requested_db % 2 != 0)))
            return 0;
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF)
    {
#ifdef SCANNER_HAVE_HACKRF
        int result;
        if (channel != 0 || (stage == SCANNER_RADIO_GAIN_LNA && (requested_db > 40 || requested_db % 8 != 0))
            || (stage == SCANNER_RADIO_GAIN_VGA && (requested_db > 62 || requested_db % 2 != 0)))
            return 0;
        result = stage == SCANNER_RADIO_GAIN_LNA
                     ? hackrf_set_lna_gain((hackrf_device *)frontend->device_handle, requested_db)
                     : hackrf_set_vga_gain((hackrf_device *)frontend->device_handle, requested_db);
        if (result != HACKRF_SUCCESS)
            return 0;
#else
        return 0;
#endif
    }
    else if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
#ifdef SCANNER_HAVE_BLADERF
        const char *stages[16];
        const char *requested_stage = stage == SCANNER_RADIO_GAIN_LNA ? "lna" : "rxvga1";
        const char *stage_name = NULL;
        int stage_count = bladerf_get_gain_stages((struct bladerf *)frontend->device_handle,
                                                  BLADERF_CHANNEL_RX(channel), stages,
                                                  sizeof(stages) / sizeof(stages[0]));
        int stage_index;
        if (stage_count < 0)
            return 0;
        for (stage_index = 0; stage_index < stage_count && stage_index < (int)(sizeof(stages) / sizeof(stages[0]));
             ++stage_index)
        {
            if (strcmp(stages[stage_index], requested_stage) == 0
                || (stage == SCANNER_RADIO_GAIN_VGA && strcmp(stages[stage_index], "vga1") == 0))
            {
                stage_name = stages[stage_index];
                break;
            }
        }
        if (stage_name == NULL)
            return -1;
        if (bladerf_set_gain_stage((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), stage_name,
                                   requested_db)
            != 0)
            return 0;
#else
        return 0;
#endif
    }
    else
        return 0;

    *stored_gain = requested_db;
    *configured = 1;
    *actual_db = requested_db;
    return 1;
}

int scanner_radio_get_total_gain(ScannerRadioInventory *inventory, uint8_t channel, int16_t *gain_cdb)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    int total_db;
    if (frontend == NULL || gain_cdb == NULL)
        return 0;
#ifdef SCANNER_HAVE_BLADERF
    if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
        int gain_db = 0;
        if (bladerf_get_gain((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), &gain_db) != 0)
            return 0;
        total_db = gain_db;
    }
    else
#endif
    {
        if (!frontend->lna_gain_configured || !frontend->vga_gain_configured)
            return -1;
        total_db = (int)frontend->configured_lna_gain_db + (int)frontend->configured_vga_gain_db;
    }
    if (total_db > INT16_MAX / 100 || total_db < INT16_MIN / 100)
        return 0;
    *gain_cdb = (int16_t)(total_db * 100);
    return 1;
}

int scanner_radio_get_gain_stages(ScannerRadioInventory *inventory, uint8_t channel, int8_t *lna_db, int8_t *vga_db)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    if (frontend == NULL || lna_db == NULL || vga_db == NULL)
        return 0;

    if (frontend->backend == SCANNER_RADIO_BACKEND_HACKRF || frontend->backend == SCANNER_RADIO_BACKEND_STUB)
    {
        if (!frontend->lna_gain_configured || !frontend->vga_gain_configured)
            return -1;
        if (frontend->configured_lna_gain_db > INT8_MAX || frontend->configured_vga_gain_db > INT8_MAX)
            return 0;
        *lna_db = (int8_t)frontend->configured_lna_gain_db;
        *vga_db = (int8_t)frontend->configured_vga_gain_db;
        return 1;
    }

#ifdef SCANNER_HAVE_BLADERF
    if (frontend->backend == SCANNER_RADIO_BACKEND_BLADERF)
    {
        int gain_db = 0;
        if (bladerf_get_gain((struct bladerf *)frontend->device_handle, BLADERF_CHANNEL_RX(channel), &gain_db) != 0)
            return 0;
        if (gain_db < INT8_MIN || gain_db > INT8_MAX)
            return 0;
        *lna_db = (int8_t)gain_db;
        *vga_db = 0;
        return 1;
    }
#endif
    return -2;
}

int scanner_radio_capture_iq(ScannerRadioInventory *inventory, uint8_t channel, uint16_t complex_pairs, uint8_t *output,
                             size_t output_capacity, size_t *output_size, uint8_t *sample_format,
                             unsigned int timeout_ms)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    size_t required_size;
    if (frontend == NULL || output == NULL || output_size == NULL || sample_format == NULL || complex_pairs == 0
        || complex_pairs > SCANNER_RADIO_MAX_CAPTURE_IQ_PAIRS || timeout_ms == 0 || !frontend->frequency_configured
        || !frontend->sample_rate_configured || !frontend->bandwidth_configured)
        return 0;

    required_size = (size_t)complex_pairs * (frontend->iq_sample_format == SCANNER_RADIO_IQ_FORMAT_S16 ? 4u : 2u);
    if (output_capacity < required_size)
        return 0;
    *output_size = 0;

    switch (frontend->backend)
    {
    case SCANNER_RADIO_BACKEND_STUB:
        return scanner_radio_capture_stub(frontend, complex_pairs, output, output_size, sample_format);
    case SCANNER_RADIO_BACKEND_HACKRF:
        return scanner_radio_capture_hackrf(frontend, channel, complex_pairs, output, required_size, output_size,
                                            sample_format, timeout_ms);
    case SCANNER_RADIO_BACKEND_BLADERF:
        return scanner_radio_capture_bladerf(frontend, channel, complex_pairs, output, required_size, output_size,
                                             sample_format, timeout_ms);
    default:
        return 0;
    }
}

int scanner_radio_prepare_capture_buffer(ScannerRadioInventory *inventory, uint8_t channel)
{
    ScannerRadioFrontend *frontend = active_frontend_mutable(inventory, channel);
    if (frontend == NULL || frontend->backend != SCANNER_RADIO_BACKEND_BLADERF)
        return 0;
    return scanner_radio_prepare_bladerf_capture(frontend);
}

int scanner_radio_measure_noise_floor(ScannerRadioInventory *inventory, uint8_t channel, uint16_t complex_pairs,
                                      int16_t *noise_floor_cdbfs, unsigned int timeout_ms)
{
    uint8_t *iq;
    uint8_t radio_format;
    uint8_t dsp_format;
    size_t size = 0;
    int result;
    if (noise_floor_cdbfs == NULL || complex_pairs == 0 || complex_pairs > SCANNER_RADIO_MAX_CAPTURE_IQ_PAIRS)
        return 0;
    iq = (uint8_t *)malloc((size_t)complex_pairs * 4u);
    if (iq == NULL)
        return 0;
    result = scanner_radio_capture_iq(inventory, channel, complex_pairs, iq, (size_t)complex_pairs * 4u, &size,
                                      &radio_format, timeout_ms);
    if (!result)
    {
        free(iq);
        return 0;
    }
    if (radio_format == SCANNER_RADIO_IQ_FORMAT_S8)
        dsp_format = SCANNER_DSP_IQ_FORMAT_S8;
    else if (radio_format == SCANNER_RADIO_IQ_FORMAT_S16)
        dsp_format = SCANNER_DSP_IQ_FORMAT_S16_Q11;
    else
    {
        free(iq);
        return 0;
    }

    result = scanner_dsp_estimate_noise_floor_dbfs(iq, size, complex_pairs, dsp_format, noise_floor_cdbfs);
    free(iq);
    return result;
}