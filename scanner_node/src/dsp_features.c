#include "dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ (-180.0)

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

int scanner_dsp_extract_features(const double *psd_dbfs_per_hz, size_t bin_count, uint32_t sample_rate_hz,
                                 double detection_threshold_db, size_t minimum_band_bins, ScannerDspFeatures *features,
                                 ScannerDspSignalBand *bands, size_t band_capacity)
{
    double *sorted;
    double *linear;
    double median_floor;
    double peak = -300.0;
    size_t peak_bin = 0;
    size_t index;
    size_t active_bins = 0;
    size_t band_count = 0;
    double bandwidth = 0.0;
    double log_sum = 0.0;
    double linear_sum = 0.0;
    int in_band = 0;
    size_t band_start = 0;

    if (psd_dbfs_per_hz == NULL || features == NULL || bin_count < 4 || sample_rate_hz == 0
        || !isfinite(detection_threshold_db) || detection_threshold_db < 0.0 || minimum_band_bins == 0
        || (band_capacity > 0 && bands == NULL))
        return 0;
    sorted = (double *)malloc(bin_count * sizeof(*sorted));
    linear = (double *)malloc(bin_count * sizeof(*linear));
    if (sorted == NULL || linear == NULL)
    {
        free(sorted);
        free(linear);
        return 0;
    }
    /* Keep exact/underflow FFT zeros from creating an artificial -300 dB median floor. */
    for (index = 0; index < bin_count; ++index)
    {
        if (!isfinite(psd_dbfs_per_hz[index]))
        {
            free(sorted);
            free(linear);
            return 0;
        }
        sorted[index] = fmax(psd_dbfs_per_hz[index], SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ);
    }
    qsort(sorted, bin_count, sizeof(*sorted), compare_double);
    median_floor = (bin_count & 1u) ? sorted[bin_count / 2u]
                                    : (sorted[bin_count / 2u - 1u] + sorted[bin_count / 2u]) * 0.5;

    for (index = 0; index < bin_count; ++index)
    {
        double feature_power = fmax(psd_dbfs_per_hz[index], SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ);
        linear[index] = pow(10.0, feature_power / 10.0);
        log_sum += feature_power * log(10.0) / 10.0;
        linear_sum += linear[index];
        if (feature_power > peak)
        {
            peak = feature_power;
            peak_bin = index;
        }
    }

    /* A quantized pure tone may have exact zero bins; use a bounded peak-relative floor in that case. */
    if (median_floor <= SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ + 0.5)
        median_floor = fmax(SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ, peak - 60.0);
    for (index = 0; index < bin_count; ++index)
    {
        double feature_power = fmax(psd_dbfs_per_hz[index], SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ);
        if (feature_power >= median_floor + detection_threshold_db)
            active_bins++;
    }

    memset(features, 0, sizeof(*features));
    features->noise_floor_dbfs_per_hz = median_floor;
    features->peak_dbfs_per_hz = peak;
    features->peak_above_floor_db = peak - median_floor;
    features->peak_offset_hz = ((double)peak_bin - (double)bin_count / 2.0) * (double)sample_rate_hz / bin_count;
    features->occupied_bandwidth_hz = (double)active_bins * sample_rate_hz / bin_count;
    features->occupied_fraction = (double)active_bins / bin_count;
    features->spectral_flatness = linear_sum > 0.0 ? exp(log_sum / bin_count) / (linear_sum / bin_count) : 0.0;

    /* Report only contiguous regions wide enough to be meaningful at this FFT resolution. */
    for (index = 0; index <= bin_count; ++index)
    {
        int is_active = index < bin_count
                        && fmax(psd_dbfs_per_hz[index], SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ)
                               >= median_floor + detection_threshold_db;
        if (is_active && !in_band)
        {
            band_start = index;
            in_band = 1;
        }
        else if (!is_active && in_band)
        {
            size_t band_end = index - 1u;
            size_t width = band_end - band_start + 1u;
            size_t band_peak = band_start;
            size_t bin;
            if (width >= minimum_band_bins)
            {
                if (band_count < band_capacity)
                {
                    for (bin = band_start + 1u; bin <= band_end; ++bin)
                    {
                        if (fmax(psd_dbfs_per_hz[bin], SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ)
                            > fmax(psd_dbfs_per_hz[band_peak], SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ))
                            band_peak = bin;
                    }
                    bands[band_count].lower_offset_hz = ((double)band_start - (double)bin_count / 2.0) * sample_rate_hz
                                                        / bin_count;
                    bands[band_count].upper_offset_hz = ((double)(band_end + 1u) - (double)bin_count / 2.0)
                                                        * sample_rate_hz / bin_count;
                    bands[band_count].peak_offset_hz = ((double)band_peak - (double)bin_count / 2.0) * sample_rate_hz
                                                       / bin_count;
                    bands[band_count].peak_dbfs_per_hz = fmax(psd_dbfs_per_hz[band_peak],
                                                              SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ);
                    bands[band_count].peak_above_floor_db = bands[band_count].peak_dbfs_per_hz - median_floor;
                }
                band_count++;
                bandwidth += (double)width * sample_rate_hz / bin_count;
            }
            in_band = 0;
        }
    }
    features->occupied_bandwidth_hz = bandwidth;
    features->signal_band_count = band_count;
    free(sorted);
    free(linear);
    return 1;
}