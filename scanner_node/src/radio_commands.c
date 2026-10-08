#include "radio_commands.h"
#include "analysis_worker.h"
#include "commutator.h"
#include "iq_recording.h"
#include "perf_probe.h"
#include "protocol.h"
#include "radio_frontend.h"
#include "scanner_log.h"
#include "signal_classifier.h"


#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define SCANNER_CAPTURE_TIMEOUT_MS 2000u

static int store_worker_response(ScannerRadioWorkerResult *worker_result, const uint8_t *response, size_t response_size)
{
    if (response_size == 0 || response_size > sizeof(worker_result->payload))
    {
        LG('!', "Radio command completion failed: response encoding size=%lu", (unsigned long)response_size);
        snprintf(worker_result->error, sizeof(worker_result->error), "%s", "Could not encode radio command response");
        return 0;
    }
    memcpy(worker_result->payload, response, response_size);
    worker_result->size = response_size;
    LG(response[5] == SCANNER_RADIO_STATUS_OK ? '+' : '!',
       "Radio command completed command=%u status=%u response-bytes=%lu", (unsigned int)response[4],
       (unsigned int)response[5], (unsigned long)response_size);
    return 1;
}

static int capture_and_record(ScannerRadioCommandContext *app, ScannerRadioInventory *inventory, uint8_t channel,
                              uint16_t complex_samples, uint8_t *iq, size_t iq_capacity, size_t *iq_size,
                              uint8_t *sample_format)
{
    const ScannerRadioFrontend *frontend;
    char error[160];
    if (!scanner_radio_capture_iq(inventory, channel, complex_samples, iq, iq_capacity, iq_size, sample_format,
                                  SCANNER_CAPTURE_TIMEOUT_MS))
        return 0;
    if (app == NULL || app->iq_file == NULL)
        return 1;
    frontend = scanner_radio_active(inventory);
    if (!scanner_iq_recording_write(app->iq_file, frontend, channel, complex_samples, *sample_format, iq, *iq_size,
                                    error, sizeof(error)))
    {
        LG('!', "IQ recording failed: %s", error);
        return 0;
    }
    return 1;
}

static int analyze_captured_window(ScannerRadioInventory *inventory, uint8_t channel, uint16_t complex_pairs,
                                   size_t fft_size, ScannerRadioCommandContext *app, int16_t *noise_floor_cdbfs,
                                   ScannerAnalysisResult *analysis_result)
{
    uint8_t iq[SCANNER_RADIO_MAX_CAPTURE_IQ_PAIRS * 4u];
    uint8_t radio_format;
    uint8_t dsp_format;
    size_t iq_size = 0;
    uint32_t sample_rate_hz = 0;
    const ScannerRadioFrontend *frontend;
    char analysis_error[160];
    if (app == NULL || app->analysis_worker == NULL || noise_floor_cdbfs == NULL
        || !capture_and_record(app, inventory, channel, complex_pairs, iq, sizeof(iq), &iq_size, &radio_format))
    {
        LG('!', "Window analysis failed: missing analysis context or capture channel=%u", (unsigned int)channel);
        return 0;
    }
    frontend = scanner_radio_active(inventory);
    if (frontend == NULL || scanner_radio_get_sample_rate(inventory, channel, &sample_rate_hz) != 1)
    {
        LG('!', "Window analysis failed: active frontend or sample rate unavailable channel=%u", (unsigned int)channel);
        return 0;
    }
    if (radio_format == SCANNER_RADIO_IQ_FORMAT_S8)
        dsp_format = SCANNER_DSP_IQ_FORMAT_S8;
    else if (radio_format == SCANNER_RADIO_IQ_FORMAT_S16)
        dsp_format = SCANNER_DSP_IQ_FORMAT_S16_Q11;
    else
    {
        LG('!', "Window analysis failed: unsupported IQ format=%u", (unsigned int)radio_format);
        return 0;
    }

    /* Send an owned bounded copy to analysis; radio handles stay in the radio worker. */
    if (!scanner_analysis_worker_analyze(app->analysis_worker, iq, iq_size, complex_pairs, dsp_format, sample_rate_hz,
                                         fft_size, 8.0, 2, analysis_result, analysis_error, sizeof(analysis_error)))
    {
        LG('!', "DSP analysis failed: %s", analysis_error);
        return 0;
    }

    /* Keep the legacy server field in window-power dBFS; PSD floor is analysis metadata. */
    *noise_floor_cdbfs = analysis_result->mean_window_power_cdbfs;
    LG('*', "DSP class=%s confidence=%.2f peak-offset=%+.0f Hz occupied-BW=%.0f Hz PSD-floor=%.2f dBFS/Hz",
       scanner_signal_class_name(analysis_result->classification.signal_class),
       analysis_result->classification.confidence, analysis_result->features.peak_offset_hz,
       analysis_result->features.occupied_bandwidth_hz, analysis_result->features.noise_floor_dbfs_per_hz);
    return 1;
}

int scanner_radio_command_handle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory,
                                 const ScannerDatagram *datagram, ScannerRadioWorkerResult *worker_result,
                                 void *context)
{
    uint8_t response[SCANNER_RADIO_COMMAND_RESPONSE_MAX_SIZE];
    size_t response_size;
    ScannerRadioFrontendsRequest frontends_request;
    ScannerSetActiveRadioRequest select_request;
    ScannerExitRequest exit_request;
    ScannerSetFrequencyRequest set_frequency_request;
    ScannerGetFrequencyRequest get_frequency_request;
    ScannerSetU32Request set_u32_request;
    ScannerGetValueRequest get_value_request;
    ScannerSetGainRequest set_gain_request;
    ScannerMeasureFrequencyRequest measure_frequency_request;
    ScannerSweepRequest sweep_request;
    ScannerRawIqRequest raw_iq_request;
    ScannerCommutatorRequest commutator_request;
    ScannerVerRequest reinitialize_request;
    const ScannerRadioFrontend *active_frontend;
    ScannerRadioCommandContext *app = (ScannerRadioCommandContext *)context;
    PERF_SCOPE("radio.command");

    if (scanner_decode_reinitialize_request(datagram->payload, datagram->size, &reinitialize_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (!scanner_radio_reinitialize(inventory))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        response_size = scanner_encode_reinitialize_response(response, reinitialize_request.request_id, status);
        return store_worker_response(worker_result, response, response_size);
    }

    if (scanner_decode_commutator_request(datagram->payload, datagram->size, &commutator_request))
    {
        uint8_t status;
        uint8_t applied_antenna = 0;
        int16_t power_cdb = 0;
        ScannerCommutatorResult result;
        if (commutator_request.channel > 1)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else
        {
            if (commutator_request.command == SCANNER_SELECT_ANTENNA_COMMAND)
                result = scanner_commutator_select_antenna(commutator_request.channel, commutator_request.value,
                                                           &applied_antenna);
            else
                result = scanner_commutator_set_path(commutator_request.channel, commutator_request.value, &power_cdb);
            status = result == SCANNER_COMMUTATOR_OK            ? SCANNER_RADIO_STATUS_OK
                     : result == SCANNER_COMMUTATOR_UNSUPPORTED ? SCANNER_RADIO_STATUS_UNSUPPORTED
                                                                : SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        }
        response_size = scanner_encode_commutator_response(response, &commutator_request, status, applied_antenna,
                                                           power_cdb);
        return store_worker_response(worker_result, response, response_size);
    }

    if (scanner_decode_radio_frontends_request(datagram->payload, datagram->size, &frontends_request))
    {
        response_size = scanner_encode_radio_frontends_response(response, sizeof(response),
                                                                frontends_request.request_id, inventory);
        if (response_size == 0)
        {
            LG('!', "Radio frontend query %" PRIu32 " failed: capabilities encoding", frontends_request.request_id);
            snprintf(worker_result->error, sizeof(worker_result->error), "%s",
                     "Could not encode radio frontend capabilities");
            worker_result->fatal = 1;
            return 0;
        }
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG('+', "Radio frontend query %" PRIu32 " answered: %lu frontend(s), active=%u", frontends_request.request_id,
           (unsigned long)inventory->count, (unsigned int)inventory->active_id);
        return 1;
    }

    if (scanner_decode_set_active_radio_request(datagram->payload, datagram->size, &select_request))
    {
        uint8_t status = scanner_radio_select(inventory, select_request.frontend_id) ? SCANNER_RADIO_STATUS_OK
                                                                                     : SCANNER_RADIO_STATUS_NOT_FOUND;
        response_size = scanner_encode_set_active_radio_response(response, select_request.request_id, status,
                                                                 inventory->active_id);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!', "Active radio selection %" PRIu32 ": frontend=%u status=%u",
           select_request.request_id, (unsigned int)select_request.frontend_id, (unsigned int)status);
        return 1;
    }

    if (scanner_decode_set_frequency_request(datagram->payload, datagram->size, &set_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t actual_frequency_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_frequency_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if ((uint64_t)set_frequency_request.frequency_khz * 1000u < active_frontend->frequency_min_hz
                 || (uint64_t)set_frequency_request.frequency_khz * 1000u > active_frontend->frequency_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!scanner_radio_set_frequency(inventory, set_frequency_request.channel,
                                              (uint64_t)set_frequency_request.frequency_khz * 1000u,
                                              &actual_frequency_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;

        response_size = scanner_encode_frequency_response(response, set_frequency_request.request_id,
                                                          SCANNER_SET_FREQUENCY_COMMAND, status,
                                                          set_frequency_request.channel, actual_frequency_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Set frequency request %" PRIu32 ": channel=%u requested=%u kHz status=%u actual=%" PRIu64 " Hz",
           set_frequency_request.request_id, (unsigned int)set_frequency_request.channel,
           (unsigned int)set_frequency_request.frequency_khz, (unsigned int)status, actual_frequency_hz);
        return 1;
    }

    if (scanner_decode_get_frequency_request(datagram->payload, datagram->size, &get_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t frequency_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_frequency_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else
        {
            int get_result = scanner_radio_get_frequency(inventory, get_frequency_request.channel, &frequency_hz);
            if (get_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (get_result < 0)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        }

        response_size = scanner_encode_frequency_response(response, get_frequency_request.request_id,
                                                          SCANNER_GET_FREQUENCY_COMMAND, status,
                                                          get_frequency_request.channel, frequency_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Get frequency request %" PRIu32 ": channel=%u status=%u frequency=%" PRIu64 " Hz",
           get_frequency_request.request_id, (unsigned int)get_frequency_request.channel, (unsigned int)status,
           frequency_hz);
        return 1;
    }

    if (scanner_decode_set_sample_rate_request(datagram->payload, datagram->size, &set_u32_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t actual_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_u32_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (set_u32_request.value < active_frontend->sample_rate_min_hz
                 || set_u32_request.value > active_frontend->sample_rate_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_SAMPLE_RATE))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_sample_rate(inventory, set_u32_request.channel, set_u32_request.value, &actual_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        response_size = scanner_encode_u32_setting_response(response, set_u32_request.request_id,
                                                            SCANNER_SET_SAMPLE_RATE_COMMAND, status,
                                                            set_u32_request.channel, actual_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Set sample rate id=%" PRIu32 " channel=%u requested=%u Hz status=%u applied=%u Hz",
           set_u32_request.request_id, (unsigned int)set_u32_request.channel, (unsigned int)set_u32_request.value,
           (unsigned int)status, (unsigned int)actual_hz);
        return 1;
    }

    if (scanner_decode_get_sample_rate_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t value_hz = 0;
        int get_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_SAMPLE_RATE))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else
        {
            get_result = scanner_radio_get_sample_rate(inventory, get_value_request.channel, &value_hz);
            if (get_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (get_result < 0)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        }
        response_size = scanner_encode_u32_setting_response(response, get_value_request.request_id,
                                                            SCANNER_GET_SAMPLE_RATE_COMMAND, status,
                                                            get_value_request.channel, value_hz);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Get sample rate id=%" PRIu32 " channel=%u status=%u value=%u Hz", get_value_request.request_id,
           (unsigned int)get_value_request.channel, (unsigned int)status, (unsigned int)value_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_set_bandwidth_request(datagram->payload, datagram->size, &set_u32_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t actual_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_u32_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (set_u32_request.value < active_frontend->bandwidth_min_hz
                 || set_u32_request.value > active_frontend->bandwidth_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_BANDWIDTH))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_bandwidth(inventory, set_u32_request.channel, set_u32_request.value, &actual_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        response_size = scanner_encode_u32_setting_response(response, set_u32_request.request_id,
                                                            SCANNER_SET_BANDWIDTH_COMMAND, status,
                                                            set_u32_request.channel, actual_hz);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Set bandwidth id=%" PRIu32 " channel=%u requested=%u Hz status=%u applied=%u Hz",
           set_u32_request.request_id, (unsigned int)set_u32_request.channel, (unsigned int)set_u32_request.value,
           (unsigned int)status, (unsigned int)actual_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_set_gain_request(datagram->payload, datagram->size, SCANNER_SET_LNA_GAIN_COMMAND,
                                        &set_gain_request)
        || scanner_decode_set_gain_request(datagram->payload, datagram->size, SCANNER_SET_VGA_GAIN_COMMAND,
                                           &set_gain_request))
    {
        uint8_t command = datagram->payload[4];
        ScannerRadioGainStage stage = command == SCANNER_SET_LNA_GAIN_COMMAND ? SCANNER_RADIO_GAIN_LNA
                                                                              : SCANNER_RADIO_GAIN_VGA;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint8_t actual_db = 0;
        int gain_result = 1;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_gain_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_GAIN))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else
        {
            gain_result = scanner_radio_set_gain_stage(inventory, set_gain_request.channel, stage,
                                                       set_gain_request.gain_db, &actual_db);
            if (gain_result < 0)
                status = SCANNER_RADIO_STATUS_UNSUPPORTED;
            else if (gain_result == 0)
                status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        }
        response_size = scanner_encode_gain_stage_response(response, set_gain_request.request_id, command, status,
                                                           set_gain_request.channel, actual_db);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Set %s gain id=%" PRIu32 " channel=%u requested=%u dB status=%u applied=%u dB",
           stage == SCANNER_RADIO_GAIN_LNA ? "LNA" : "VGA", set_gain_request.request_id,
           (unsigned int)set_gain_request.channel, (unsigned int)set_gain_request.gain_db, (unsigned int)status,
           (unsigned int)actual_db);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_get_gain_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        int16_t gain_cdb = 0;
        int gain_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_GAIN))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else
        {
            gain_result = scanner_radio_get_total_gain(inventory, get_value_request.channel, &gain_cdb);
            if (gain_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (gain_result < 0)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        }
        response_size = scanner_encode_gain_response(response, get_value_request.request_id, status,
                                                     get_value_request.channel, gain_cdb);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!', "Get gain id=%" PRIu32 " channel=%u status=%u value=%d cdb",
           get_value_request.request_id, (unsigned int)get_value_request.channel, (unsigned int)status, (int)gain_cdb);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_get_gain_stages_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        int8_t lna_db = 0;
        int8_t vga_db = 0;
        int gain_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_GAIN))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else
        {
            gain_result = scanner_radio_get_gain_stages(inventory, get_value_request.channel, &lna_db, &vga_db);
            if (gain_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (gain_result == -1)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
            else if (gain_result == -2)
                status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        }
        response_size = scanner_encode_gain_stages_response(response, get_value_request.request_id, status,
                                                            get_value_request.channel, lna_db, vga_db);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Get gain stages id=%" PRIu32 " channel=%u status=%u LNA=%d dB VGA=%d dB", get_value_request.request_id,
           (unsigned int)get_value_request.channel, (unsigned int)status, (int)lna_db, (int)vga_db);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_measure_current_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        int16_t noise_floor_cdbfs = 0;
        ScannerAnalysisResult analysis_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!analyze_captured_window(inventory, get_value_request.channel, 1024, 512, app, &noise_floor_cdbfs,
                                          &analysis_result))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        response_size = scanner_encode_power_response(response, get_value_request.request_id,
                                                      SCANNER_MEASURE_CURRENT_COMMAND, status,
                                                      get_value_request.channel, noise_floor_cdbfs);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Measure current id=%" PRIu32 " channel=%u status=%u power=%d cdbfs", get_value_request.request_id,
           (unsigned int)get_value_request.channel, (unsigned int)status, (int)noise_floor_cdbfs);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_measure_frequency_request(datagram->payload, datagram->size, &measure_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t actual_hz = 0;
        int16_t noise_floor_cdbfs = 0;
        ScannerAnalysisResult analysis_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (measure_frequency_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_frequency(inventory, measure_frequency_request.channel,
                                              (uint64_t)measure_frequency_request.frequency_khz * 1000u, &actual_hz))
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!analyze_captured_window(inventory, measure_frequency_request.channel, 1024, 512, app,
                                          &noise_floor_cdbfs, &analysis_result))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        response_size = scanner_encode_power_response(response, measure_frequency_request.request_id,
                                                      SCANNER_MEASURE_FREQUENCY_COMMAND, status,
                                                      measure_frequency_request.channel, noise_floor_cdbfs);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Measure frequency id=%" PRIu32 " channel=%u requested=%u kHz status=%u power=%d cdbfs",
           measure_frequency_request.request_id, (unsigned int)measure_frequency_request.channel,
           (unsigned int)measure_frequency_request.frequency_khz, (unsigned int)status, (int)noise_floor_cdbfs);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    /* SWEEP returns one DSP-estimated noise-floor level per tuned frequency. */
    if (scanner_decode_sweep_request(datagram->payload, datagram->size, &sweep_request))
    {
        LG('i', "Sweep id=%" PRIu32 " channel=%u start=%u stop=%u step=%u kHz", sweep_request.request_id,
           (unsigned int)sweep_request.channel, (unsigned int)sweep_request.start_khz,
           (unsigned int)sweep_request.stop_khz, (unsigned int)sweep_request.step_khz);
        uint32_t frequencies[SCANNER_MAX_SWEEP_POINTS];
        int16_t noise_floors_cdbfs[SCANNER_MAX_SWEEP_POINTS];
        uint16_t count = 0;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t point_count;
        uint64_t current_khz;
        ScannerAnalysisResult analysis_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (sweep_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (sweep_request.step_khz == 0 || sweep_request.start_khz > sweep_request.stop_khz
                 || (uint64_t)sweep_request.start_khz * 1000u < active_frontend->frequency_min_hz
                 || (uint64_t)sweep_request.stop_khz * 1000u > active_frontend->frequency_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        point_count = status == SCANNER_RADIO_STATUS_OK
                          ? ((uint64_t)sweep_request.stop_khz - sweep_request.start_khz) / sweep_request.step_khz + 1u
                          : 0u;
        if (status == SCANNER_RADIO_STATUS_OK && point_count > SCANNER_MAX_SWEEP_POINTS)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        if (status == SCANNER_RADIO_STATUS_OK && !(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        if (status == SCANNER_RADIO_STATUS_OK)
        {
            for (current_khz = sweep_request.start_khz; current_khz <= sweep_request.stop_khz;
                 current_khz += sweep_request.step_khz)
            {
                uint64_t actual_hz = 0;
                uint32_t rounded_khz;
                if (scanner_radio_worker_current_cancelled(worker))
                {
                    LG('!', "Sweep id=%" PRIu32 " cancelled at %" PRIu64 " kHz", sweep_request.request_id, current_khz);
                    status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
                    count = 0;
                    break;
                }
                frequencies[count] = (uint32_t)current_khz;
                if (!scanner_radio_set_frequency(inventory, sweep_request.channel, current_khz * 1000u, &actual_hz)
                    || !analyze_captured_window(inventory, sweep_request.channel, 4096, 1024, app,
                                                &noise_floors_cdbfs[count], &analysis_result))
                {
                    status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
                    count = 0;
                    break;
                }
                rounded_khz = (uint32_t)(actual_hz / 1000u);
                frequencies[count] = rounded_khz;
                count++;
                if (sweep_request.stop_khz - current_khz < sweep_request.step_khz)
                    break;
            }
        }
        response_size = scanner_encode_sweep_response(response, sizeof(response), sweep_request.request_id, status,
                                                      sweep_request.channel, count, frequencies, noise_floors_cdbfs);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!', "Sweep id=%" PRIu32 " status=%u points=%u",
           sweep_request.request_id, (unsigned int)status, (unsigned int)count);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_save_iq_request(datagram->payload, datagram->size, &raw_iq_request))
    {
        uint8_t iq[SCANNER_MAX_RAW_IQ_PAIRS * 4u];
        size_t iq_size = 0;
        uint8_t format = 0;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        active_frontend = scanner_radio_active(inventory);
        if (app == NULL || app->iq_file == NULL)
            status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        else if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (raw_iq_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!capture_and_record(app, inventory, raw_iq_request.channel, raw_iq_request.complex_pairs, iq,
                                     sizeof(iq), &iq_size, &format))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;

        response_size = scanner_encode_iq_file_response(response, raw_iq_request.request_id, status,
                                                        raw_iq_request.channel,
                                                        status == SCANNER_RADIO_STATUS_OK ? format : 0,
                                                        status == SCANNER_RADIO_STATUS_OK ? raw_iq_request.complex_pairs
                                                                                          : 0,
                                                        status == SCANNER_RADIO_STATUS_OK ? (uint32_t)iq_size : 0);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Save IQ id=%" PRIu32 " channel=%u status=%u samples=%u bytes=%lu", raw_iq_request.request_id,
           (unsigned int)raw_iq_request.channel, (unsigned int)status, (unsigned int)raw_iq_request.complex_pairs,
           (unsigned long)iq_size);
        return 1;
    }

    /* Raw-IQ requests return the captured bytes and optionally tee the same bytes to the file. */
    if (scanner_decode_raw_iq_request(datagram->payload, datagram->size, &raw_iq_request))
    {
        uint8_t iq[SCANNER_MAX_RAW_IQ_PAIRS * 4u];
        size_t iq_size = 0;
        uint8_t format = 0;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (raw_iq_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!capture_and_record(app, inventory, raw_iq_request.channel, raw_iq_request.complex_pairs, iq,
                                     sizeof(iq), &iq_size, &format))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        response_size = scanner_encode_raw_iq_response(response, sizeof(response), raw_iq_request.request_id, status,
                                                       raw_iq_request.channel, format,
                                                       status == SCANNER_RADIO_STATUS_OK ? raw_iq_request.complex_pairs
                                                                                         : 0,
                                                       status == SCANNER_RADIO_STATUS_OK ? iq : NULL,
                                                       status == SCANNER_RADIO_STATUS_OK ? iq_size : 0);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Raw IQ id=%" PRIu32 " channel=%u status=%u samples=%u bytes=%lu", raw_iq_request.request_id,
           (unsigned int)raw_iq_request.channel, (unsigned int)status, (unsigned int)raw_iq_request.complex_pairs,
           (unsigned long)iq_size);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_exit_request(datagram->payload, datagram->size, &exit_request))
    {
        response_size = scanner_encode_exit_response(response, exit_request.request_id, SCANNER_RADIO_STATUS_OK);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        worker_result->close_after_send = 1;
        LG('i', "Exit command %" PRIu32 " acknowledged; shutting down", exit_request.request_id);
        return 2;
    }

    LG('!', "Ignoring malformed or unsupported radio command");
    return 1;
}
