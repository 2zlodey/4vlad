#include "analysis_worker.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


int main(void)
{
    uint8_t iq[1024u * 2u];
    ScannerAnalysisWorker *worker = NULL;
    ScannerAnalysisResult result;
    char error[256];
    size_t sample;
    int16_t repeat_noise_floor;
    int ok = 1;

    for (sample = 0; sample < 1024; ++sample)
    {
        double phase = 2.0 * 3.14159265358979323846 * 16.0 * (double)sample / 256.0;
        int8_t i_value = (int8_t)lrint(70.0 * cos(phase));
        int8_t q_value = (int8_t)lrint(70.0 * sin(phase));
        iq[sample * 2u] = (uint8_t)i_value;
        iq[sample * 2u + 1u] = (uint8_t)q_value;
    }

    if (!scanner_analysis_worker_start(&worker, error, sizeof(error)))
    {
        fprintf(stderr, "FAIL: could not start analysis worker: %s\n", error);
        return 1;
    }
    if (!scanner_analysis_worker_analyze(worker, iq, sizeof(iq), 1024, SCANNER_DSP_IQ_FORMAT_S8, 512000, 256, 10.0, 2,
                                         &result, error, sizeof(error)))
    {
        fprintf(stderr, "FAIL: analysis worker failed: %s\n", error);
        scanner_analysis_worker_stop(&worker);
        return 1;
    }
    if (result.classification.signal_class != SCANNER_SIGNAL_NARROWBAND_TONE
        || fabs(result.features.peak_offset_hz - 32000.0) > 1000.0 || result.band_count == 0)
    {
        fprintf(stderr, "analysis features: class=%d peak=%.1fHz rise=%.2fdB occupied=%.1fHz fraction=%.4f bands=%lu\n",
                (int)result.classification.signal_class, result.features.peak_offset_hz,
                result.features.peak_above_floor_db, result.features.occupied_bandwidth_hz,
                result.features.occupied_fraction, (unsigned long)result.band_count);
        fprintf(stderr, "FAIL: analysis result did not preserve the expected tone features\n");
        ok = 0;
    }
    repeat_noise_floor = (int16_t)lrint(result.features.noise_floor_dbfs_per_hz * 100.0);
    if (!scanner_analysis_worker_analyze(worker, iq, sizeof(iq), 1024, SCANNER_DSP_IQ_FORMAT_S8, 512000, 256, 10.0, 2,
                                         &result, error, sizeof(error))
        || repeat_noise_floor != (int16_t)lrint(result.features.noise_floor_dbfs_per_hz * 100.0))
    {
        fprintf(stderr, "FAIL: repeated analysis was not deterministic\n");
        ok = 0;
    }
    scanner_analysis_worker_stop(&worker);
    if (worker != NULL)
    {
        fprintf(stderr, "FAIL: analysis worker was not joined and cleared\n");
        ok = 0;
    }
    if (ok)
        puts("Analysis worker tests passed.");
    return ok ? 0 : 1;
}
