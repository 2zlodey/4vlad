#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "radio_capture_backend.h"
#include "scanner_log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef SCANNER_HAVE_BLADERF
#include <libbladeRF.h>
#endif

#ifdef SCANNER_HAVE_HACKRF
#include <libhackrf/hackrf.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#endif

#ifdef SCANNER_HAVE_HACKRF
typedef struct
{
    uint8_t *output;
    size_t capacity;
    size_t bytes_written;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
} HackrfCaptureContext;

static void hackrf_context_lock(HackrfCaptureContext *context)
{
#ifdef _WIN32
    EnterCriticalSection(&context->mutex);
#else
    (void)pthread_mutex_lock(&context->mutex);
#endif
}

static void hackrf_context_unlock(HackrfCaptureContext *context)
{
#ifdef _WIN32
    LeaveCriticalSection(&context->mutex);
#else
    (void)pthread_mutex_unlock(&context->mutex);
#endif
}

static int hackrf_context_init(HackrfCaptureContext *context)
{
#ifdef _WIN32
    InitializeCriticalSection(&context->mutex);
    return 1;
#else
    return pthread_mutex_init(&context->mutex, NULL) == 0;
#endif
}

static void hackrf_context_destroy(HackrfCaptureContext *context)
{
#ifdef _WIN32
    DeleteCriticalSection(&context->mutex);
#else
    (void)pthread_mutex_destroy(&context->mutex);
#endif
}

static int hackrf_capture_callback(hackrf_transfer *transfer)
{
    HackrfCaptureContext *context;
    size_t remaining;
    size_t copy_size;
    if (transfer == NULL || transfer->rx_ctx == NULL || transfer->buffer == NULL || transfer->valid_length < 0)
        return -1;
    context = (HackrfCaptureContext *)transfer->rx_ctx;
    hackrf_context_lock(context);
    remaining = context->capacity - context->bytes_written;
    copy_size = (size_t)transfer->valid_length;
    if (copy_size > remaining)
        copy_size = remaining;
    memcpy(context->output + context->bytes_written, transfer->buffer, copy_size);
    context->bytes_written += copy_size;
    hackrf_context_unlock(context);
    return 0;
}

int scanner_radio_capture_hackrf(ScannerRadioFrontend *frontend, uint8_t channel, uint16_t complex_pairs,
                                 uint8_t *output, size_t required_size, size_t *output_size, uint8_t *sample_format,
                                 unsigned int timeout_ms)
{
    HackrfCaptureContext context;
    unsigned int waited_ms;
    int stop_result;
    size_t bytes_written;
    hackrf_device *device = (hackrf_device *)frontend->device_handle;
    (void)complex_pairs;
    if (channel != 0 || frontend->iq_sample_format != SCANNER_RADIO_IQ_FORMAT_S8 || !hackrf_context_init(&context))
    {
        LG('!', "HackRF capture setup failed: channel, sample format or mutex initialization");
        return 0;
    }

    context.output = output;
    context.capacity = required_size;
    context.bytes_written = 0;
    if (hackrf_start_rx(device, hackrf_capture_callback, &context) != HACKRF_SUCCESS)
    {
        LG('!', "HackRF RX start failed");
        hackrf_context_destroy(&context);
        return 0;
    }
    for (waited_ms = 0; waited_ms < timeout_ms; ++waited_ms)
    {
        int capture_complete;
        hackrf_context_lock(&context);
        capture_complete = context.bytes_written >= context.capacity;
        hackrf_context_unlock(&context);
        if (capture_complete)
            break;
#ifdef _WIN32
        Sleep(1);
#else
        {
            struct timespec delay = { 0, 1000000L };
            nanosleep(&delay, NULL);
        }
#endif
        if (hackrf_is_streaming(device) != HACKRF_TRUE && !capture_complete)
            break;
    }

    stop_result = hackrf_stop_rx(device);
    hackrf_context_lock(&context);
    bytes_written = context.bytes_written;
    hackrf_context_unlock(&context);
    hackrf_context_destroy(&context);
    if (bytes_written != context.capacity || stop_result != HACKRF_SUCCESS)
    {
        LG('!', "HackRF RX incomplete or stop failed bytes=%lu required=%lu stop-status=%d waited=%u ms",
           (unsigned long)bytes_written, (unsigned long)context.capacity, stop_result, waited_ms);
        return 0;
    }
    *output_size = bytes_written;
    *sample_format = SCANNER_RADIO_IQ_FORMAT_S8;
    return 1;
}
#else
int scanner_radio_capture_hackrf(ScannerRadioFrontend *frontend, uint8_t channel, uint16_t complex_pairs,
                                 uint8_t *output, size_t required_size, size_t *output_size, uint8_t *sample_format,
                                 unsigned int timeout_ms)
{
    (void)frontend;
    (void)channel;
    (void)complex_pairs;
    (void)output;
    (void)required_size;
    (void)output_size;
    (void)sample_format;
    (void)timeout_ms;
    LG('!', "HackRF capture unavailable: backend disabled");
    return 0;
}
#endif

typedef struct
{
    uint64_t frequency_hz;
    int16_t power_cdbfs;
} StubSpectrumPoint;

static int16_t stub_target_power_cdbfs(uint64_t frequency_hz)
{
    static const StubSpectrumPoint profile[] = {
        { UINT64_C(100000000), -1200 },  { UINT64_C(300000000), -2200 },  { UINT64_C(450000000), -900 },
        { UINT64_C(650000000), -2000 },  { UINT64_C(900000000), -1000 },  { UINT64_C(1200000000), -2100 },
        { UINT64_C(1450000000), -1600 }, { UINT64_C(1600000000), -800 },  { UINT64_C(1850000000), -1000 },
        { UINT64_C(2100000000), -2200 }, { UINT64_C(2360000000), -700 },  { UINT64_C(2600000000), -1900 },
        { UINT64_C(2800000000), -1000 }, { UINT64_C(3100000000), -2200 }, { UINT64_C(3500000000), -900 },
        { UINT64_C(3900000000), -1800 }, { UINT64_C(4300000000), -1200 }, { UINT64_C(4700000000), -2100 },
        { UINT64_C(5100000000), -800 },  { UINT64_C(5500000000), -1900 }, { UINT64_C(5800000000), -700 },
        { UINT64_C(6000000000), -1600 }
    };
    size_t index;
    if (frequency_hz <= profile[0].frequency_hz)
        return profile[0].power_cdbfs;
    for (index = 1; index < sizeof(profile) / sizeof(profile[0]); ++index)
    {
        if (frequency_hz <= profile[index].frequency_hz)
        {
            uint64_t left_hz = profile[index - 1].frequency_hz;
            uint64_t span_hz = profile[index].frequency_hz - left_hz;
            int32_t power_delta = (int32_t)profile[index].power_cdbfs - profile[index - 1].power_cdbfs;
            int64_t interpolated = (int64_t)profile[index - 1].power_cdbfs
                                   + (int64_t)power_delta * (int64_t)(frequency_hz - left_hz) / (int64_t)span_hz;
            return (int16_t)interpolated;
        }
    }
    return profile[sizeof(profile) / sizeof(profile[0]) - 1].power_cdbfs;
}

int scanner_radio_capture_stub(ScannerRadioFrontend *frontend, uint16_t complex_pairs, uint8_t *output,
                               size_t *output_size, uint8_t *sample_format)
{
    size_t index;
    size_t required_size = (size_t)complex_pairs * 2u;
    static const int8_t fixed_iq_cycle[] = { 1, 1, -1, 1, -1, -1, 1, -1 };
    long double target_dbfs;
    long double base_dbfs;
    long double amplitude_scale;
    int64_t amplitude;
    if (!frontend->frequency_configured || !frontend->sample_rate_configured || !frontend->bandwidth_configured
        || !frontend->lna_gain_configured || !frontend->vga_gain_configured)
    {
        LG('!', "Stub capture failed: frequency, sample rate, bandwidth or gain not configured");
        return 0;
    }

    target_dbfs = (long double)stub_target_power_cdbfs(frontend->configured_frequency_hz) / 100.0L;
    base_dbfs = 20.0L * log10l(74.0L / 128.0L);
    amplitude_scale = powl(10.0L, (target_dbfs - base_dbfs) / 20.0L);
    amplitude = llroundl(74.0L * amplitude_scale);
    for (index = 0; index < required_size; ++index)
        output[index] = (uint8_t)(int8_t)(fixed_iq_cycle[index % sizeof(fixed_iq_cycle)] * amplitude);
    *output_size = required_size;
    *sample_format = SCANNER_RADIO_IQ_FORMAT_S8;
    return 1;
}

#ifdef SCANNER_HAVE_BLADERF
static int ensure_bladerf_capture_buffer(ScannerRadioFrontend *frontend)
{
    size_t capacity;
    if (frontend->capture_buffer != NULL)
        return 1;
    capacity = (size_t)SCANNER_RADIO_MAX_CAPTURE_IQ_PAIRS * frontend->rx_channels * 2u * sizeof(int16_t);
    frontend->capture_buffer = malloc(capacity);
    LG(frontend->capture_buffer != NULL ? '+' : '!', "BladeRF capture buffer allocation %s bytes=%lu",
       frontend->capture_buffer != NULL ? "completed" : "failed", (unsigned long)capacity);
    return frontend->capture_buffer != NULL;
}
#endif

int scanner_radio_prepare_bladerf_capture(ScannerRadioFrontend *frontend)
{
    if (frontend == NULL || frontend->backend != SCANNER_RADIO_BACKEND_BLADERF || frontend->rx_channels == 0
        || frontend->rx_channels > 2)
    {
        LG('!', "BladeRF capture preparation failed: invalid frontend or channel count");
        return 0;
    }
#ifdef SCANNER_HAVE_BLADERF
    return ensure_bladerf_capture_buffer(frontend);
#else
    LG('!', "BladeRF capture preparation unavailable: backend disabled");
    return 0;
#endif
}

#ifdef SCANNER_HAVE_BLADERF
int scanner_radio_capture_bladerf(ScannerRadioFrontend *frontend, uint8_t channel, uint16_t complex_pairs,
                                  uint8_t *output, size_t required_size, size_t *output_size, uint8_t *sample_format,
                                  unsigned int timeout_ms)
{
    unsigned int channel_count = frontend->rx_channels;
    size_t pair_index;
    int16_t *raw;
    struct bladerf *device = (struct bladerf *)frontend->device_handle;
    bladerf_channel_layout layout;
    int result;
    if (frontend->iq_sample_format != SCANNER_RADIO_IQ_FORMAT_S16 || channel_count == 0)
    {
        LG('!', "BladeRF capture failed: invalid sample format or channel count");
        return 0;
    }
    layout = channel_count > 1 ? BLADERF_RX_X2 : BLADERF_RX_X1;
    if (!ensure_bladerf_capture_buffer(frontend))
        return 0;
    raw = (int16_t *)frontend->capture_buffer;
    if (bladerf_sync_config(device, layout, BLADERF_FORMAT_SC16_Q11, 8, 4096, 4, timeout_ms) != 0)
    {
        LG('!', "BladeRF synchronous RX configuration failed");
        return 0;
    }
    if (channel_count > 1)
    {
        if (bladerf_enable_module(device, BLADERF_CHANNEL_RX(0), true) != 0
            || bladerf_enable_module(device, BLADERF_CHANNEL_RX(1), true) != 0)
        {
            LG('!', "BladeRF dual-channel RX enable failed");
            if (bladerf_enable_module(device, BLADERF_CHANNEL_RX(0), false) != 0)
                LG('!', "BladeRF RX channel 0 cleanup disable failed");
            if (bladerf_enable_module(device, BLADERF_CHANNEL_RX(1), false) != 0)
                LG('!', "BladeRF RX channel 1 cleanup disable failed");
            return 0;
        }
    }
    else if (bladerf_enable_module(device, BLADERF_CHANNEL_RX(0), true) != 0)
    {
        LG('!', "BladeRF RX enable failed");
        return 0;
    }
    {
        struct bladerf_metadata metadata;
        memset(&metadata, 0, sizeof(metadata));
        result = bladerf_sync_rx(device, raw, complex_pairs, &metadata, timeout_ms);
    }
    for (pair_index = 0; pair_index < channel_count; ++pair_index)
    {
        if (bladerf_enable_module(device, BLADERF_CHANNEL_RX((unsigned int)pair_index), false) != 0)
            LG('!', "BladeRF RX disable failed channel=%u", (unsigned int)pair_index);
    }
    if (result != 0)
    {
        LG('!', "BladeRF synchronous RX failed status=%d", result);
        return 0;
    }
    for (pair_index = 0; pair_index < complex_pairs; ++pair_index)
    {
        size_t source_index = (pair_index * channel_count + channel) * 2u;
        size_t dest_index = pair_index * 4u;
        uint16_t i_value = (uint16_t)raw[source_index];
        uint16_t q_value = (uint16_t)raw[source_index + 1u];
        output[dest_index] = (uint8_t)(i_value & 0xffu);
        output[dest_index + 1u] = (uint8_t)(i_value >> 8);
        output[dest_index + 2u] = (uint8_t)(q_value & 0xffu);
        output[dest_index + 3u] = (uint8_t)(q_value >> 8);
    }
    *output_size = required_size;
    *sample_format = SCANNER_RADIO_IQ_FORMAT_S16;
    return 1;
}
#else
int scanner_radio_capture_bladerf(ScannerRadioFrontend *frontend, uint8_t channel, uint16_t complex_pairs,
                                  uint8_t *output, size_t required_size, size_t *output_size, uint8_t *sample_format,
                                  unsigned int timeout_ms)
{
    (void)frontend;
    (void)channel;
    (void)complex_pairs;
    (void)output;
    (void)required_size;
    (void)output_size;
    (void)sample_format;
    (void)timeout_ms;
    LG('!', "BladeRF capture unavailable: backend disabled");
    return 0;
}
#endif