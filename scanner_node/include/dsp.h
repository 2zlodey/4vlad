#ifndef SCANNER_NODE_DSP_H
#define SCANNER_NODE_DSP_H

#include <stddef.h>
#include <stdint.h>

#define SCANNER_DSP_IQ_FORMAT_S8 1u
#define SCANNER_DSP_IQ_FORMAT_S16_Q11 2u
#define SCANNER_DSP_MIN_FFT_SIZE 64u
#define SCANNER_DSP_MAX_FFT_SIZE 4096u
#define SCANNER_DSP_MAX_SIGNAL_BANDS 16u

/*
 * Estimate the average complex-sample power of one captured I/Q window.
 * The result is a normalized dBFS level in signed centi-dB, not calibrated dBm.
 * This estimate does not separate noise from a coherent signal; it is the
 * current noise-floor proxy used by MEASURE and each SWEEP point.
 */
int scanner_dsp_estimate_noise_floor_dbfs(const uint8_t *iq, size_t iq_size, size_t complex_sample_count,
                                          uint8_t sample_format, int16_t *noise_floor_cdbfs);

typedef struct
{
    double lower_offset_hz;
    double upper_offset_hz;
    double peak_offset_hz;
    double peak_dbfs_per_hz;
    double peak_above_floor_db;
} ScannerDspSignalBand;

typedef struct
{
    double noise_floor_dbfs_per_hz;
    double peak_dbfs_per_hz;
    double peak_above_floor_db;
    double peak_offset_hz;
    double occupied_bandwidth_hz;
    double spectral_flatness;
    double occupied_fraction;
    size_t signal_band_count;
} ScannerDspFeatures;

typedef enum
{
    SCANNER_SIGNAL_UNKNOWN = 0,
    SCANNER_SIGNAL_NARROWBAND_TONE = 1,
    SCANNER_SIGNAL_MULTICARRIER = 2,
    SCANNER_SIGNAL_WIDEBAND_NOISELIKE = 3
} ScannerSignalClass;

typedef struct
{
    ScannerSignalClass signal_class;
    double confidence;
    const char *classifier_name;
} ScannerClassification;

/*
 * Compute a Hann-windowed, 50%-overlap Welch PSD for complex baseband I/Q.
 * Output has fft_size fft-shifted bins spanning [-sample_rate/2, +sample_rate/2)
 * and is expressed in dBFS/Hz. Caller supplies output storage for fft_size bins.
 */
int scanner_dsp_welch_psd_dbfs_hz(const uint8_t *iq, size_t iq_size, size_t complex_sample_count, uint8_t sample_format,
                                  uint32_t sample_rate_hz, size_t fft_size, double *output_dbfs_per_hz,
                                  size_t output_capacity, size_t *output_bin_count);

/* Estimate median floor, above-floor bands, peak location, and occupied bandwidth. */
int scanner_dsp_extract_features(const double *psd_dbfs_per_hz, size_t bin_count, uint32_t sample_rate_hz,
                                 double detection_threshold_db, size_t minimum_band_bins, ScannerDspFeatures *features,
                                 ScannerDspSignalBand *bands, size_t band_capacity);

#endif
