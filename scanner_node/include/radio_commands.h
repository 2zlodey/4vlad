#ifndef SCANNER_NODE_RADIO_COMMANDS_H
#define SCANNER_NODE_RADIO_COMMANDS_H

#include "analysis_worker.h"
#include "radio_worker.h"

#include <stdio.h>

#define SCANNER_DETECT_INTERNAL_MAX_WINDOWS 8192u

typedef struct
{
    ScannerDetectCandidate wire;
    uint64_t lower_hz;
    uint64_t upper_hz;
} ScannerDetectorCandidate;

typedef struct
{
    uint32_t next_job_id;
    uint32_t job_id;
    uint64_t start_hz;
    uint64_t stop_hz;
    uint64_t capture_width_hz;
    uint64_t hop_hz;
    double psd_linear_sum[SCANNER_DSP_MAX_FFT_SIZE];
    ScannerDetectorCandidate candidates[SCANNER_DETECT_MAX_CANDIDATES];
    uint16_t fft_size;
    uint16_t capture_pairs_per_frame;
    uint32_t dwell_ms;
    uint32_t frames_per_window;
    uint32_t frame_index;
    uint32_t window_index;
    uint32_t window_count;
    uint16_t progress;
    uint32_t candidate_count;
    int16_t sensitivity_cdb;
    uint32_t pfa_ppm;
    uint8_t training_bins_per_side;
    uint8_t guard_bins_per_side;
    uint8_t channel;
    uint8_t frontend_id;
    uint8_t antenna_id;
    uint8_t algorithm_id;
    uint8_t state;
    uint8_t failure_status;
    uint32_t sample_rate_hz;
    uint32_t bandwidth_hz;
} ScannerDetectorJob;

typedef struct
{
    const char *software_version;
    ScannerAnalysisWorker *analysis_worker;
    FILE *iq_file;
    const ScannerDevice *device;
    ScannerDetectorJob detector_job;
} ScannerRadioCommandContext;

int scanner_radio_command_handle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory,
                                 const ScannerDatagram *datagram, ScannerRadioWorkerResult *worker_result,
                                 void *context);
unsigned int scanner_radio_detector_idle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory, void *context);

#endif
