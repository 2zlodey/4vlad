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
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SCANNER_CAPTURE_TIMEOUT_MS 2000u

static uint32_t hz_to_khz_rounded(uint64_t frequency_hz)
{
    uint64_t value_khz = (frequency_hz + 500u) / 1000u;
    return value_khz > UINT32_MAX ? UINT32_MAX : (uint32_t)value_khz;
}

static int is_known_command(uint8_t command)
{
    switch (command)
    {
    case SCANNER_VER_COMMAND:
    case SCANNER_MEASURE_FREQUENCY_COMMAND:
    case SCANNER_SWEEP_COMMAND:
    case SCANNER_GET_RADIO_FRONTENDS_COMMAND:
    case SCANNER_SET_ACTIVE_RADIO_COMMAND:
    case SCANNER_SET_FREQUENCY_COMMAND:
    case SCANNER_SET_SAMPLE_RATE_COMMAND:
    case SCANNER_SET_LNA_GAIN_COMMAND:
    case SCANNER_SET_VGA_GAIN_COMMAND:
    case SCANNER_SET_BANDWIDTH_COMMAND:
    case SCANNER_REINITIALIZE_SDR_COMMAND:
    case SCANNER_GET_FREQUENCY_COMMAND:
    case SCANNER_GET_GAIN_COMMAND:
    case SCANNER_GET_GAIN_STAGES_COMMAND:
    case SCANNER_GET_SAMPLE_RATE_COMMAND:
    case SCANNER_MEASURE_CURRENT_COMMAND:
    case SCANNER_GET_RAW_IQ_COMMAND:
    case SCANNER_SAVE_IQ_TO_FILE_COMMAND:
    case SCANNER_DETECT_START_COMMAND:
    case SCANNER_DETECT_GET_RESULT_COMMAND:
    case SCANNER_EXIT_COMMAND:
    case SCANNER_SELECT_ANTENNA_COMMAND:
    case SCANNER_SET_PATH_COMMAND:
        return 1;
    default:
        return 0;
    }
}

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
    LG('+', "Radio command response ready (%lu bytes)", (unsigned long)response_size);
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

/* Standard Orkestr has no radio-setup exchange. Lazily open an RX frontend and
 * establish conservative defaults without overriding explicit user settings. */
static int prepare_standard_rx(ScannerRadioInventory *inventory, uint8_t channel)
{
    const ScannerRadioFrontend *f = scanner_radio_active(inventory);
    uint32_t actual, target;
    uint8_t gain;
    size_t i;
    if (f == NULL || f->device_handle == NULL)
    {
        for (i = 0; i < inventory->count; ++i)
            if ((inventory->frontends[i].capabilities & SCANNER_RADIO_CAP_RX)
                && channel < inventory->frontends[i].rx_channels
                && scanner_radio_select(inventory, inventory->frontends[i].id))
                break;
        f = scanner_radio_active(inventory);
    }
    if (f == NULL || f->device_handle == NULL || channel >= f->rx_channels)
        return 0;
    if (!f->sample_rate_configured)
    {
        target = 2000000u;
        if (target < f->sample_rate_min_hz)
            target = f->sample_rate_min_hz;
        if (target > f->sample_rate_max_hz)
            target = f->sample_rate_max_hz;
        if (f->sample_rate_step_hz)
            target = f->sample_rate_min_hz
                     + (target - f->sample_rate_min_hz) / f->sample_rate_step_hz * f->sample_rate_step_hz;
        if (!scanner_radio_set_sample_rate(inventory, channel, target, &actual))
            return 0;
    }
    if (!f->bandwidth_configured)
    {
        target = 1750000u;
        if (target < f->bandwidth_min_hz)
            target = f->bandwidth_min_hz;
        if (target > f->bandwidth_max_hz)
            target = f->bandwidth_max_hz;
        if (f->bandwidth_option_count)
            target = f->bandwidth_options[0];
        else if (f->bandwidth_step_hz)
            target = f->bandwidth_min_hz + (target - f->bandwidth_min_hz) / f->bandwidth_step_hz * f->bandwidth_step_hz;
        if (!scanner_radio_set_bandwidth(inventory, channel, target, &actual))
            return 0;
    }
    if (f->backend == SCANNER_RADIO_BACKEND_HACKRF || f->backend == SCANNER_RADIO_BACKEND_STUB)
    {
        if (!f->lna_gain_configured
            && !scanner_radio_set_gain_stage(inventory, channel, SCANNER_RADIO_GAIN_LNA, 16, &gain))
            return 0;
        if (!f->vga_gain_configured
            && !scanner_radio_set_gain_stage(inventory, channel, SCANNER_RADIO_GAIN_VGA, 16, &gain))
            return 0;
    }
    return 1;
}

static const ScannerAntenna *find_antenna(const ScannerDevice *device, int32_t antenna_input)
{
    size_t index;
    if (device == NULL)
        return NULL;
    for (index = 0; index < SCANNER_NODE_ANTENNA_COUNT; ++index)
        if (device->antennas[index].input == antenna_input)
            return &device->antennas[index];
    return NULL;
}

static int resolve_detector_antenna(const ScannerDevice *device, uint8_t channel, uint8_t requested_id,
                                    uint8_t *effective_id, const ScannerAntenna **antenna)
{
    const ScannerAntenna *selected;
    if (device == NULL || channel >= SCANNER_NODE_MAX_RX_CHANNELS || effective_id == NULL || antenna == NULL)
        return 0;
    selected = find_antenna(device, requested_id);
    if (selected == NULL)
        return 0;
    if (device->antenna_routes[channel].mode == SCANNER_ANTENNA_ROUTE_FIXED)
    {
        selected = find_antenna(device, device->antenna_routes[channel].fixed_antenna_input);
        if (selected == NULL)
            return 0;
    }
    *effective_id = (uint8_t)selected->input;
    *antenna = selected;
    return 1;
}

static uint32_t quantize_rate_up(const ScannerRadioFrontend *frontend, uint64_t desired_hz)
{
    uint64_t value = desired_hz;
    if (value < frontend->sample_rate_min_hz)
        value = frontend->sample_rate_min_hz;
    if (value > frontend->sample_rate_max_hz)
        value = frontend->sample_rate_max_hz;
    if (frontend->sample_rate_step_hz > 0)
    {
        uint64_t delta = value - frontend->sample_rate_min_hz;
        uint64_t steps = (delta + frontend->sample_rate_step_hz - 1u) / frontend->sample_rate_step_hz;
        value = frontend->sample_rate_min_hz + steps * frontend->sample_rate_step_hz;
        if (value > frontend->sample_rate_max_hz)
            value = frontend->sample_rate_max_hz;
    }
    return (uint32_t)value;
}

static int choose_detector_bandwidth(const ScannerRadioFrontend *frontend, uint32_t desired_hz, uint32_t rate_limit_hz,
                                     uint32_t *selected_hz)
{
    uint32_t upper = frontend->bandwidth_max_hz < rate_limit_hz ? frontend->bandwidth_max_hz : rate_limit_hz;
    if (selected_hz == NULL || upper < frontend->bandwidth_min_hz)
        return 0;
    if (frontend->bandwidth_option_count > 0)
    {
        size_t index;
        uint32_t smallest = UINT32_MAX;
        uint32_t largest = 0;
        for (index = 0; index < frontend->bandwidth_option_count; ++index)
        {
            uint32_t option = frontend->bandwidth_options[index];
            if (option < frontend->bandwidth_min_hz || option > upper)
                continue;
            if (option > largest)
                largest = option;
            if (option >= desired_hz && option < smallest)
                smallest = option;
        }
        *selected_hz = smallest != UINT32_MAX ? smallest : largest;
        return *selected_hz != 0;
    }
    {
        uint64_t selected = desired_hz;
        if (selected < frontend->bandwidth_min_hz)
            selected = frontend->bandwidth_min_hz;
        if (selected > upper)
            selected = upper;
        if (frontend->bandwidth_step_hz > 0 && selected < upper)
        {
            uint64_t delta = selected - frontend->bandwidth_min_hz;
            uint64_t steps = (delta + frontend->bandwidth_step_hz - 1u) / frontend->bandwidth_step_hz;
            selected = frontend->bandwidth_min_hz + steps * frontend->bandwidth_step_hz;
            if (selected > upper)
                selected = upper;
        }
        *selected_hz = (uint32_t)selected;
        return 1;
    }
}

static int configure_detector_capture(ScannerRadioInventory *inventory, uint8_t channel, uint64_t requested_width_hz,
                                      uint32_t *sample_rate_hz, uint32_t *bandwidth_hz, uint64_t *usable_width_hz)
{
    const ScannerRadioFrontend *frontend = scanner_radio_active(inventory);
    uint64_t desired_width = requested_width_hz + requested_width_hz / 10u;
    uint64_t rate_target_hz;
    uint32_t rate_limit_hz;
    uint32_t selected_bandwidth;
    uint32_t selected_rate;
    uint32_t actual_rate;
    uint32_t actual_bandwidth;
    if (frontend == NULL || sample_rate_hz == NULL || bandwidth_hz == NULL || usable_width_hz == NULL)
        return 0;
    rate_limit_hz = frontend->sample_rate_max_hz;
    rate_target_hz = desired_width > rate_limit_hz ? rate_limit_hz : desired_width;
    if (desired_width > frontend->bandwidth_max_hz)
        desired_width = frontend->bandwidth_max_hz;
    if (!choose_detector_bandwidth(frontend, (uint32_t)desired_width, rate_limit_hz, &selected_bandwidth))
        return 0;
    selected_rate = quantize_rate_up(frontend, rate_target_hz);
    if (!scanner_radio_set_sample_rate(inventory, channel, selected_rate, &actual_rate)
        || !choose_detector_bandwidth(frontend, selected_bandwidth, actual_rate, &selected_bandwidth)
        || !scanner_radio_set_bandwidth(inventory, channel, selected_bandwidth, &actual_bandwidth))
        return 0;
    *sample_rate_hz = actual_rate;
    *bandwidth_hz = actual_bandwidth;
    *usable_width_hz = (actual_rate < actual_bandwidth ? actual_rate : actual_bandwidth) * 9u / 10u;
    LG('*', "Detector capture plan channel=%u mode=single-RX sample-rate=%u Hz BW=%u Hz usable=%llu Hz max-rate=%u Hz",
       (unsigned int)channel, (unsigned int)actual_rate, (unsigned int)actual_bandwidth,
       (unsigned long long)*usable_width_hz, (unsigned int)frontend->sample_rate_max_hz);
    return *usable_width_hz > 0;
}

static uint64_t detector_window_center_hz(const ScannerDetectorJob *job)
{
    uint64_t requested_span = job->stop_hz - job->start_hz;
    uint64_t covered_span = job->capture_width_hz;
    uint64_t remaining;
    if (job->window_count <= 1u || requested_span <= covered_span)
        return job->start_hz + requested_span / 2u;
    remaining = requested_span - covered_span;
    return job->start_hz + covered_span / 2u + remaining * job->window_index / (job->window_count - 1u);
}

static int16_t detector_db_to_cdb(double db)
{
    double value = db * 100.0;
    if (value > INT16_MAX)
        value = INT16_MAX;
    if (value < INT16_MIN)
        value = INT16_MIN;
    return (int16_t)lrint(value);
}

static void detector_add_candidate(ScannerDetectorJob *job, uint64_t lower_hz, uint64_t upper_hz, uint64_t center_hz,
                                   double power_dbfs, double snr_db, uint64_t bin_width_hz)
{
    size_t index;
    uint64_t tolerance = bin_width_hz * 2u;
    if (tolerance < 1000u)
        tolerance = 1000u;
    for (index = 0; index < job->candidate_count; ++index)
    {
        ScannerDetectCandidate *candidate = &job->candidates[index].wire;
        uint64_t expanded_lower = job->candidates[index].lower_hz > tolerance
                                      ? job->candidates[index].lower_hz - tolerance
                                      : 0u;
        uint64_t expanded_upper = job->candidates[index].upper_hz + tolerance;
        if (lower_hz <= expanded_upper && upper_hz >= expanded_lower)
        {
            if (power_dbfs > candidate->power_cdbfs / 100.0)
            {
                candidate->center_hz = center_hz;
                candidate->power_cdbfs = detector_db_to_cdb(power_dbfs);
                candidate->snr_cdb = detector_db_to_cdb(snr_db);
            }
            if (lower_hz < job->candidates[index].lower_hz)
                job->candidates[index].lower_hz = lower_hz;
            if (upper_hz > job->candidates[index].upper_hz)
                job->candidates[index].upper_hz = upper_hz;
            {
                uint64_t merged_width = job->candidates[index].upper_hz - job->candidates[index].lower_hz;
                candidate->bandwidth_hz = merged_width > UINT32_MAX ? UINT32_MAX : (uint32_t)merged_width;
            }
            return;
        }
    }
    if (job->candidate_count >= SCANNER_DETECT_MAX_CANDIDATES)
    {
        job->failure_status = SCANNER_RADIO_STATUS_RESULT_LIMIT;
        job->state = SCANNER_DETECT_STATE_FAILED;
        return;
    }
    job->candidates[job->candidate_count].wire.center_hz = center_hz;
    job->candidates[job->candidate_count].wire.bandwidth_hz = upper_hz - lower_hz > UINT32_MAX
                                                                  ? UINT32_MAX
                                                                  : (uint32_t)(upper_hz - lower_hz);
    job->candidates[job->candidate_count].wire.power_cdbfs = detector_db_to_cdb(power_dbfs);
    job->candidates[job->candidate_count].wire.snr_cdb = detector_db_to_cdb(snr_db);
    job->candidates[job->candidate_count].lower_hz = lower_hz;
    job->candidates[job->candidate_count].upper_hz = upper_hz;
    job->candidate_count++;
}

unsigned int scanner_radio_detector_idle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory, void *context)
{
    ScannerRadioCommandContext *app = (ScannerRadioCommandContext *)context;
    ScannerDetectorJob *job;
    const ScannerRadioFrontend *frontend;
    uint16_t capture_pairs;
    uint8_t iq[SCANNER_RADIO_MAX_CAPTURE_IQ_PAIRS * 4u];
    uint8_t radio_format;
    uint8_t dsp_format;
    size_t iq_size = 0;
    uint32_t sample_rate_hz;
    double psd[SCANNER_DSP_MAX_FFT_SIZE];
    uint8_t active_bins[SCANNER_DSP_MAX_FFT_SIZE];
    size_t bin_count = 0;
    size_t index;
    uint32_t bandwidth_hz;
    uint64_t center_hz;
    (void)worker;
    if (app == NULL || inventory == NULL)
        return 100u;
    job = &app->detector_job;
    if (job->state != SCANNER_DETECT_STATE_RUNNING)
        return 100u;

    frontend = scanner_radio_active(inventory);
    if (frontend == NULL || job->channel >= frontend->rx_channels || frontend->id != job->frontend_id
        || scanner_radio_get_sample_rate(inventory, job->channel, &sample_rate_hz) != 1
        || scanner_radio_get_bandwidth(inventory, job->channel, &bandwidth_hz) != 1
        || sample_rate_hz != job->sample_rate_hz || bandwidth_hz != job->bandwidth_hz)
    {
        job->state = SCANNER_DETECT_STATE_FAILED;
        job->failure_status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        return 100u;
    }
    center_hz = detector_window_center_hz(job);
    if (!scanner_radio_set_frequency_no_readback(inventory, job->channel, center_hz))
    {
        job->state = SCANNER_DETECT_STATE_FAILED;
        job->failure_status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        return 100u;
    }
    if (job->frame_index == 0)
        memset(job->psd_linear_sum, 0, sizeof(job->psd_linear_sum));
    capture_pairs = job->capture_pairs_per_frame;
    if (!capture_and_record(app, inventory, job->channel, capture_pairs, iq, sizeof(iq), &iq_size, &radio_format))
    {
        job->state = SCANNER_DETECT_STATE_FAILED;
        job->failure_status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        return 100u;
    }
    if (radio_format == SCANNER_RADIO_IQ_FORMAT_S8)
        dsp_format = SCANNER_DSP_IQ_FORMAT_S8;
    else if (radio_format == SCANNER_RADIO_IQ_FORMAT_S16)
        dsp_format = SCANNER_DSP_IQ_FORMAT_S16_Q11;
    else
    {
        job->state = SCANNER_DETECT_STATE_FAILED;
        job->failure_status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        return 100u;
    }
    if (!scanner_dsp_welch_psd_dbfs_hz(iq, iq_size, capture_pairs, dsp_format, sample_rate_hz, job->fft_size, psd,
                                       SCANNER_DSP_MAX_FFT_SIZE, &bin_count))
    {
        job->state = SCANNER_DETECT_STATE_FAILED;
        job->failure_status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        return 100u;
    }
    for (index = 0; index < bin_count; ++index)
        job->psd_linear_sum[index] += pow(10.0, psd[index] / 10.0);

    job->frame_index++;
    if (job->frame_index >= job->frames_per_window)
    {
        for (index = 0; index < bin_count; ++index)
            psd[index] = 10.0 * log10(fmax(job->psd_linear_sum[index] / job->frames_per_window, 1e-300));
        if (!scanner_dsp_cfar_detect(psd, bin_count, job->training_bins_per_side, job->guard_bins_per_side,
                                     (double)job->pfa_ppm / 1000000.0, active_bins, SCANNER_DSP_MAX_FFT_SIZE))
        {
            job->state = SCANNER_DETECT_STATE_FAILED;
            job->failure_status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
            return 100u;
        }
        for (index = 0; index < bin_count;)
        {
            size_t start;
            size_t end;
            size_t peak_bin;
            double snr_db;
            double lower_offset;
            double upper_offset;
            double center_offset;
            double signal_power = 0.0;
            double noise_density = 0.0;
            double candidate_width_hz;
            double power_dbfs;
            double absolute_lower_hz;
            double absolute_upper_hz;
            double absolute_center_hz;
            uint64_t lower_hz;
            uint64_t upper_hz;
            uint64_t candidate_center_hz;
            while (index < bin_count && !active_bins[index])
                index++;
            if (index == bin_count)
                break;
            start = index;
            peak_bin = index;
            while (index < bin_count && active_bins[index])
            {
                if (psd[index] > psd[peak_bin])
                    peak_bin = index;
                index++;
            }
            end = index;
            if (end - start < 2u)
                continue;
            for (size_t signal_bin = start; signal_bin < end; ++signal_bin)
                signal_power += pow(10.0, psd[signal_bin] / 10.0) * sample_rate_hz / bin_count;
            {
                size_t training_span = job->training_bins_per_side + job->guard_bins_per_side + 4u;
                size_t neighbor_start = peak_bin > training_span ? peak_bin - training_span : 0u;
                size_t neighbor_end = peak_bin + training_span < bin_count ? peak_bin + training_span : bin_count - 1u;
                size_t noise_count = 0;
                size_t neighbor;
                for (neighbor = neighbor_start; neighbor <= neighbor_end; ++neighbor)
                {
                    if (neighbor + job->guard_bins_per_side < start || neighbor > end + job->guard_bins_per_side)
                    {
                        noise_density += pow(10.0, psd[neighbor] / 10.0);
                        noise_count++;
                    }
                }
                if (noise_count > 0)
                    noise_density /= noise_count;
            }
            candidate_width_hz = (double)(end - start) * sample_rate_hz / bin_count;
            if (signal_power <= 0.0 || noise_density <= 0.0 || candidate_width_hz <= 0.0)
                continue;
            power_dbfs = 10.0 * log10(signal_power);
            snr_db = 10.0 * log10(signal_power / (noise_density * candidate_width_hz));
            if (snr_db * 100.0 < job->sensitivity_cdb)
                continue;
            lower_offset = ((double)start - (double)bin_count / 2.0) * sample_rate_hz / bin_count;
            upper_offset = ((double)end - (double)bin_count / 2.0) * sample_rate_hz / bin_count;
            center_offset = (lower_offset + upper_offset) * 0.5;
            absolute_lower_hz = (double)center_hz + lower_offset;
            absolute_upper_hz = (double)center_hz + upper_offset;
            absolute_center_hz = (double)center_hz + center_offset;
            if (absolute_upper_hz <= (double)job->start_hz || absolute_lower_hz >= (double)job->stop_hz)
                continue;
            lower_hz = (uint64_t)fmin((double)job->stop_hz, fmax((double)job->start_hz, absolute_lower_hz));
            upper_hz = (uint64_t)fmin((double)job->stop_hz, fmax((double)job->start_hz, absolute_upper_hz));
            candidate_center_hz = (uint64_t)fmin((double)job->stop_hz, fmax((double)job->start_hz, absolute_center_hz));
            if (upper_hz > lower_hz)
                detector_add_candidate(job, lower_hz, upper_hz, candidate_center_hz, power_dbfs, snr_db,
                                       sample_rate_hz / bin_count);
            if (job->state == SCANNER_DETECT_STATE_FAILED)
                return 100u;
        }
        job->frame_index = 0;
        job->window_index++;
    }

    {
        uint64_t done = (uint64_t)job->window_index * job->frames_per_window + job->frame_index;
        uint64_t total = (uint64_t)job->window_count * job->frames_per_window;
        job->progress = total == 0 ? 1000u : (uint16_t)(done * 1000u / total);
    }
    if (job->window_index >= job->window_count)
        job->state = SCANNER_DETECT_STATE_COMPLETE;
    if (job->state == SCANNER_DETECT_STATE_COMPLETE)
        job->progress = 1000u;
    return job->state == SCANNER_DETECT_STATE_RUNNING ? 2u : 100u;
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
    ScannerReinitializeRequest reinitialize_request;
    const ScannerRadioFrontend *active_frontend;
    ScannerRadioCommandContext *app = (ScannerRadioCommandContext *)context;
    PERF_SCOPE("radio.command");

    uint32_t ver_id;
    if (scanner_decode_ver_request(datagram->payload, datagram->size, &ver_id))
    {
        response_size = scanner_encode_ver_response(response, ver_id,
                                                    app && app->software_version ? app->software_version : "1.1.0.0");
        return store_worker_response(worker_result, response, response_size);
    }
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
        else if (commutator_request.command == SCANNER_SELECT_ANTENNA_COMMAND
                 && find_antenna(app == NULL ? NULL : app->device, commutator_request.value) == NULL)
            status = SCANNER_RADIO_STATUS_NOT_FOUND;
        else
        {
            uint8_t selected_antenna = commutator_request.value;
            if (commutator_request.command == SCANNER_SELECT_ANTENNA_COMMAND && app != NULL && app->device != NULL
                && app->device->antenna_routes[commutator_request.channel].mode == SCANNER_ANTENNA_ROUTE_FIXED)
                selected_antenna = (uint8_t)app->device->antenna_routes[commutator_request.channel].fixed_antenna_input;
            if (commutator_request.command == SCANNER_SELECT_ANTENNA_COMMAND)
                result = scanner_commutator_select_antenna(commutator_request.channel, commutator_request.value,
                                                           &applied_antenna);
            else
                result = scanner_commutator_set_path(commutator_request.channel, commutator_request.value, &power_cdb);
            status = result == SCANNER_COMMUTATOR_OK            ? SCANNER_RADIO_STATUS_OK
                     : result == SCANNER_COMMUTATOR_UNSUPPORTED ? SCANNER_RADIO_STATUS_UNSUPPORTED
                                                                : SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            if (commutator_request.command == SCANNER_SELECT_ANTENNA_COMMAND && status == SCANNER_RADIO_STATUS_OK)
                applied_antenna = selected_antenna;
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
        uint32_t actual_frequency_khz = 0;
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
        if (status == SCANNER_RADIO_STATUS_OK)
            actual_frequency_khz = hz_to_khz_rounded(actual_frequency_hz);

        response_size = scanner_encode_frequency_response(response, set_frequency_request.request_id,
                                                          SCANNER_SET_FREQUENCY_COMMAND, status,
                                                          set_frequency_request.channel, actual_frequency_khz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Set frequency request %" PRIu32 ": channel=%u requested=%u kHz status=%u actual=%u kHz",
           set_frequency_request.request_id, (unsigned int)set_frequency_request.channel,
           (unsigned int)set_frequency_request.frequency_khz, (unsigned int)status, actual_frequency_khz);
        return 1;
    }

    if (scanner_decode_get_frequency_request(datagram->payload, datagram->size, &get_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t frequency_hz = 0;
        uint32_t frequency_khz = 0;
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
        if (status == SCANNER_RADIO_STATUS_OK)
            frequency_khz = hz_to_khz_rounded(frequency_hz);

        response_size = scanner_encode_frequency_response(response, get_frequency_request.request_id,
                                                          SCANNER_GET_FREQUENCY_COMMAND, status,
                                                          get_frequency_request.channel, frequency_khz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Get frequency request %" PRIu32 ": channel=%u status=%u frequency=%u kHz", get_frequency_request.request_id,
           (unsigned int)get_frequency_request.channel, (unsigned int)status, frequency_khz);
        return 1;
    }

    if (scanner_decode_set_sample_rate_request(datagram->payload, datagram->size, &set_u32_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t actual_hz = 0;
        uint64_t requested_hz = (uint64_t)set_u32_request.value * 1000u;
        uint32_t actual_khz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_u32_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (requested_hz > UINT32_MAX || requested_hz < active_frontend->sample_rate_min_hz
                 || requested_hz > active_frontend->sample_rate_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_SAMPLE_RATE))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_sample_rate(inventory, set_u32_request.channel, (uint32_t)requested_hz, &actual_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        if (status == SCANNER_RADIO_STATUS_OK)
            actual_khz = hz_to_khz_rounded(actual_hz);
        response_size = scanner_encode_u32_setting_response(response, set_u32_request.request_id,
                                                            SCANNER_SET_SAMPLE_RATE_COMMAND, status,
                                                            set_u32_request.channel, actual_khz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Set sample rate id=%" PRIu32 " channel=%u requested=%u kHz status=%u applied=%u kHz",
           set_u32_request.request_id, (unsigned int)set_u32_request.channel, (unsigned int)set_u32_request.value,
           (unsigned int)status, (unsigned int)actual_khz);
        return 1;
    }

    if (scanner_decode_get_sample_rate_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t value_hz = 0;
        uint32_t value_khz = 0;
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
        if (status == SCANNER_RADIO_STATUS_OK)
            value_khz = hz_to_khz_rounded(value_hz);
        response_size = scanner_encode_u32_setting_response(response, get_value_request.request_id,
                                                            SCANNER_GET_SAMPLE_RATE_COMMAND, status,
                                                            get_value_request.channel, value_khz);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
           "Get sample rate id=%" PRIu32 " channel=%u status=%u value=%u kHz", get_value_request.request_id,
           (unsigned int)get_value_request.channel, (unsigned int)status, (unsigned int)value_khz);
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

    {
        ScannerDetectStartRequest detect_request;
        if (scanner_decode_detect_start_request(datagram->payload, datagram->size, &detect_request))
        {
            uint8_t status = SCANNER_RADIO_STATUS_OK;
            uint32_t job_id = 0;
            uint8_t effective_antenna_id = SCANNER_RADIO_ID_NONE;
            uint64_t usable_width_hz = 0;
            uint64_t span_hz;
            uint64_t window_count;
            uint64_t target_samples;
            uint64_t samples_per_window;
            uint32_t detector_sample_rate_hz = 0;
            uint32_t detector_bandwidth_hz = 0;
            const ScannerAntenna *antenna = NULL;
            ScannerDetectorJob *job = app == NULL ? NULL : &app->detector_job;
            active_frontend = scanner_radio_active(inventory);
            if (app == NULL)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
            else if (job->state == SCANNER_DETECT_STATE_RUNNING)
                status = SCANNER_RADIO_STATUS_BUSY;
            else if (detect_request.algorithm_id != SCANNER_DETECT_ALGORITHM_CA_CFAR)
                status = SCANNER_RADIO_STATUS_UNSUPPORTED;
            else if (active_frontend != NULL && active_frontend->device_handle != NULL
                     && detect_request.channel >= active_frontend->rx_channels)
                status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
            else if (!resolve_detector_antenna(app->device, detect_request.channel, detect_request.antenna_id,
                                               &effective_antenna_id, &antenna))
                status = SCANNER_RADIO_STATUS_NOT_FOUND;
            else if (!prepare_standard_rx(inventory, detect_request.channel))
                status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
            active_frontend = scanner_radio_active(inventory);
            if (status == SCANNER_RADIO_STATUS_OK
                && (active_frontend == NULL || detect_request.channel >= active_frontend->rx_channels))
                status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
            if (status == SCANNER_RADIO_STATUS_OK
                && ((uint64_t)detect_request.start_khz * 1000u < active_frontend->frequency_min_hz
                    || (uint64_t)detect_request.stop_khz * 1000u > active_frontend->frequency_max_hz))
                status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
            span_hz = (uint64_t)(detect_request.stop_khz - detect_request.start_khz) * 1000u;
            if (status == SCANNER_RADIO_STATUS_OK)
            {
                if (!configure_detector_capture(inventory, detect_request.channel, span_hz, &detector_sample_rate_hz,
                                                &detector_bandwidth_hz, &usable_width_hz))
                    status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            }
            if (status == SCANNER_RADIO_STATUS_OK)
            {
                uint64_t hop_hz = usable_width_hz * 8u / 10u;
                uint32_t next_job_id = job->next_job_id + 1u;
                if (next_job_id == 0)
                    next_job_id = 1u;
                window_count = span_hz <= usable_width_hz ? 1u
                               : hop_hz == 0              ? UINT64_MAX
                                                          : (span_hz - usable_width_hz + hop_hz - 1u) / hop_hz + 1u;
                if (window_count > SCANNER_DETECT_INTERNAL_MAX_WINDOWS)
                    status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
                else
                {
                    target_samples = (uint64_t)detect_request.dwell_ms * detector_sample_rate_hz / 1000u;
                    samples_per_window = (target_samples + window_count - 1u) / window_count;
                    memset(job, 0, sizeof(*job));
                    job->next_job_id = next_job_id;
                    job->job_id = next_job_id;
                    job->channel = detect_request.channel;
                    job->frontend_id = active_frontend->id;
                    job->antenna_id = effective_antenna_id;
                    job->algorithm_id = detect_request.algorithm_id;
                    job->sample_rate_hz = detector_sample_rate_hz;
                    job->bandwidth_hz = detector_bandwidth_hz;
                    job->start_hz = (uint64_t)detect_request.start_khz * 1000u;
                    job->stop_hz = (uint64_t)detect_request.stop_khz * 1000u;
                    job->capture_width_hz = usable_width_hz;
                    job->hop_hz = hop_hz;
                    job->fft_size = detect_request.fft_size;
                    job->capture_pairs_per_frame = SCANNER_RADIO_MAX_CAPTURE_IQ_PAIRS;
                    job->dwell_ms = detect_request.dwell_ms;
                    job->sensitivity_cdb = detect_request.sensitivity_cdb;
                    job->pfa_ppm = detect_request.pfa_ppm;
                    job->training_bins_per_side = detect_request.training_bins_per_side;
                    job->guard_bins_per_side = detect_request.guard_bins_per_side;
                    job->window_count = (uint32_t)window_count;
                    job->frames_per_window = (uint32_t)((samples_per_window + job->capture_pairs_per_frame - 1u)
                                                        / job->capture_pairs_per_frame);
                    if (job->frames_per_window == 0)
                        job->frames_per_window = 1u;
                    job->state = SCANNER_DETECT_STATE_RUNNING;
                    job_id = job->job_id;
                }
            }
            response_size = scanner_encode_detect_start_response(response, detect_request.request_id, status, job_id,
                                                                 effective_antenna_id);
            if (!store_worker_response(worker_result, response, response_size))
                return 0;
            LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!',
               "Detector start id=%" PRIu32 " job=%" PRIu32
               " channel=%u antenna=%u algorithm=%u range=%u..%u kHz "
               "dwell=%u ms windows=%u status=%u",
               detect_request.request_id, job_id, detect_request.channel, effective_antenna_id,
               detect_request.algorithm_id, detect_request.start_khz, detect_request.stop_khz, detect_request.dwell_ms,
               status == SCANNER_RADIO_STATUS_OK ? job->window_count : 0u, (unsigned int)status);
            if (status == SCANNER_RADIO_STATUS_OK && antenna != NULL)
                LG('*', "Antenna table input=%d type=%d gain=%d dBi from=%d to=%d polar=%.1f direction=%.1f",
                   (int)antenna->input, (int)antenna->type, (int)antenna->dbi, (int)antenna->from, (int)antenna->to,
                   antenna->polar, antenna->direction);
            return 1;
        }
    }

    {
        ScannerDetectResultRequest detect_request;
        if (scanner_decode_detect_result_request(datagram->payload, datagram->size, &detect_request))
        {
            ScannerDetectorJob *job = app == NULL ? NULL : &app->detector_job;
            uint8_t status = SCANNER_RADIO_STATUS_OK;
            uint8_t state = SCANNER_DETECT_STATE_IDLE;
            uint16_t progress = 0;
            uint8_t antenna_id = 0;
            uint8_t algorithm_id = 0;
            uint32_t total_candidates = 0;
            uint32_t candidate_offset = detect_request.candidate_offset;
            uint16_t candidate_count = 0;
            ScannerDetectCandidate page[SCANNER_DETECT_RESULT_PAGE_SIZE];
            const ScannerDetectCandidate *candidates = page;
            uint32_t remaining;
            uint16_t page_index;
            if (job == NULL || job->job_id != detect_request.job_id)
                status = SCANNER_RADIO_STATUS_NOT_FOUND;
            else
            {
                state = job->state;
                progress = job->progress;
                antenna_id = job->antenna_id;
                algorithm_id = job->algorithm_id;
                total_candidates = job->candidate_count;
                if (detect_request.candidate_offset > job->candidate_count)
                {
                    status = SCANNER_RADIO_STATUS_INVALID;
                    candidate_offset = 0;
                }
                else
                {
                    remaining = job->candidate_count - detect_request.candidate_offset;
                    candidate_count = remaining > SCANNER_DETECT_RESULT_PAGE_SIZE ? SCANNER_DETECT_RESULT_PAGE_SIZE
                                                                                  : (uint16_t)remaining;
                    for (page_index = 0; page_index < candidate_count; ++page_index)
                        page[page_index] = job->candidates[detect_request.candidate_offset + page_index].wire;
                }
                if (state == SCANNER_DETECT_STATE_FAILED)
                    status = job->failure_status != 0 ? job->failure_status : SCANNER_RADIO_STATUS_CAPTURE_ERROR;
            }
            response_size = scanner_encode_detect_result_response(response, sizeof(response), detect_request.request_id,
                                                                  status, detect_request.job_id, state, progress,
                                                                  antenna_id, algorithm_id, total_candidates,
                                                                  candidate_offset, candidates, candidate_count);
            if (!store_worker_response(worker_result, response, response_size))
                return 0;
            return 1;
        }
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
        if (!prepare_standard_rx(inventory, measure_frequency_request.channel))
        {
            LG('!', "Standard RX preparation failed");
            return 1;
        }
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
        if (status != SCANNER_RADIO_STATUS_OK)
            return 1;
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    /* Standard range reply returns only the strongest sampled window. */
    if (scanner_decode_sweep_request(datagram->payload, datagram->size, &sweep_request))
    {
        LG('i', "Sweep id=%" PRIu32 " channel=%u start=%u stop=%u step=%u kHz", sweep_request.request_id,
           (unsigned int)sweep_request.channel, (unsigned int)sweep_request.start_khz,
           (unsigned int)sweep_request.stop_khz, (unsigned int)sweep_request.step_khz);
        if (!prepare_standard_rx(inventory, sweep_request.channel))
        {
            LG('!', "Standard RX preparation failed");
            return 1;
        }
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
                          ? ((uint64_t)sweep_request.stop_khz - sweep_request.start_khz + sweep_request.step_khz - 1u)
                                    / sweep_request.step_khz
                                + 1u
                          : 0u;
        if (status == SCANNER_RADIO_STATUS_OK && point_count > SCANNER_MAX_SWEEP_POINTS)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        if (status == SCANNER_RADIO_STATUS_OK && !(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        if (status == SCANNER_RADIO_STATUS_OK)
        {
            for (current_khz = sweep_request.start_khz; current_khz <= sweep_request.stop_khz;
                 current_khz = current_khz + sweep_request.step_khz < sweep_request.stop_khz
                                   ? current_khz + sweep_request.step_khz
                                   : sweep_request.stop_khz)
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
                if (current_khz == sweep_request.stop_khz)
                    break;
            }
        }
        response_size = scanner_encode_sweep_response(response, sizeof(response), sweep_request.request_id, status,
                                                      sweep_request.channel, count, frequencies, noise_floors_cdbfs);
        LG(status == SCANNER_RADIO_STATUS_OK ? '+' : '!', "Sweep id=%" PRIu32 " status=%u points=%u",
           sweep_request.request_id, (unsigned int)status, (unsigned int)count);
        if (status != SCANNER_RADIO_STATUS_OK)
            return 1;
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

    {
        uint32_t request_id;
        uint8_t command;
        if (scanner_decode_command_header(datagram->payload, datagram->size, &request_id, &command)
            && !is_known_command(command))
        {
            response_size = scanner_encode_unsupported_response(response, request_id, command);
            if (!store_worker_response(worker_result, response, response_size))
                return 0;
            LG('i', "Unsupported command 0x%02x answered NOT_SUPPORTED", (unsigned int)command);
            return 1;
        }
    }

    LG('!', "Ignoring malformed radio command");
    return 1;
}
