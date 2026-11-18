#ifndef SCANNER_NODE_RADIO_COMMANDS_H
#define SCANNER_NODE_RADIO_COMMANDS_H

#include "analysis_worker.h"
#include "radio_worker.h"

#include <stdio.h>

typedef struct
{
    uint32_t next_job_id;
    uint32_t job_id;
    uint32_t start_khz;
    uint32_t stop_khz;
    uint32_t step_khz;
    uint32_t current_khz;
    uint64_t best_center_hz;
    uint64_t best_lower_hz;
    uint64_t best_upper_hz;
    double psd_linear_sum[SCANNER_DSP_MAX_FFT_SIZE];
    uint16_t fft_size;
    uint16_t averages;
    uint16_t average_index;
    uint16_t frequency_index;
    uint16_t frequency_count;
    uint16_t progress;
    uint16_t detection_count;
    int16_t best_snr_cdbfs;
    uint8_t channel;
    uint8_t frontend_id;
    uint8_t state;
    uint8_t iq_recorded;
    uint32_t sample_rate_hz;
    uint32_t bandwidth_hz;
} ScannerDetectorJob;

typedef struct
{
    const char *software_version;
    ScannerAnalysisWorker *analysis_worker;
    FILE *iq_file;
    ScannerDetectorJob detector_job;
} ScannerRadioCommandContext;

int scanner_radio_command_handle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory,
                                 const ScannerDatagram *datagram, ScannerRadioWorkerResult *worker_result,
                                 void *context);
void scanner_radio_detector_idle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory, void *context);

#endif
