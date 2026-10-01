#ifndef SCANNER_NODE_ANALYSIS_WORKER_H
#define SCANNER_NODE_ANALYSIS_WORKER_H

#include "dsp.h"

#include <stddef.h>
#include <stdint.h>

#define SCANNER_ANALYSIS_QUEUE_CAPACITY 4u
#define SCANNER_ANALYSIS_MAX_IQ_BYTES 65536u

typedef struct ScannerAnalysisWorker ScannerAnalysisWorker;

typedef struct
{
    int16_t mean_window_power_cdbfs;
    ScannerDspFeatures features;
    ScannerDspSignalBand bands[SCANNER_DSP_MAX_SIGNAL_BANDS];
    size_t band_count;
    ScannerClassification classification;
} ScannerAnalysisResult;

int scanner_analysis_worker_start(ScannerAnalysisWorker **worker, char *error, size_t error_size);
int scanner_analysis_worker_analyze(ScannerAnalysisWorker *worker, const uint8_t *iq, size_t iq_size,
                                    size_t complex_sample_count, uint8_t sample_format, uint32_t sample_rate_hz,
                                    size_t fft_size, double detection_threshold_db, size_t minimum_band_bins,
                                    ScannerAnalysisResult *result, char *error, size_t error_size);
void scanner_analysis_worker_stop(ScannerAnalysisWorker **worker);

#endif
