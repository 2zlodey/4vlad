#include "dsp.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCANNER_DSP_FEATURE_NUMERICAL_FLOOR_DBFS_PER_HZ (-180.0)

static int32_t decode_s8(uint8_t value) { return value <= INT8_MAX ? (int32_t)value : (int32_t)value - 256; }

static int32_t decode_s16_le(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    return value <= INT16_MAX ? (int32_t)value : (int32_t)value - 65536;
}

typedef struct
{
    double real;
    double imaginary;
} ScannerDspComplex;

static int is_power_of_two(size_t value) { return value != 0 && (value & (value - 1u)) == 0; }

static void fft_forward(ScannerDspComplex *values, size_t count)
{
    size_t index;
    size_t reversed = 0;
    size_t length;
    for (index = 1; index < count; ++index)
    {
        size_t bit = count >> 1;
        while (reversed & bit)
        {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed)
        {
            ScannerDspComplex temporary = values[index];
            values[index] = values[reversed];
            values[reversed] = temporary;
        }
    }

    for (length = 2; length <= count; length <<= 1)
    {
        double angle = -2.0 * 3.14159265358979323846 / (double)length;
        double root_real = cos(angle);
        double root_imaginary = sin(angle);
        size_t base;
        for (base = 0; base < count; base += length)
        {
            double twiddle_real = 1.0;
            double twiddle_imaginary = 0.0;
            size_t offset;
            for (offset = 0; offset < length / 2u; ++offset)
            {
                ScannerDspComplex even = values[base + offset];
                ScannerDspComplex odd = values[base + offset + length / 2u];
                double product_real = odd.real * twiddle_real - odd.imaginary * twiddle_imaginary;
                double product_imaginary = odd.real * twiddle_imaginary + odd.imaginary * twiddle_real;
                values[base + offset].real = even.real + product_real;
                values[base + offset].imaginary = even.imaginary + product_imaginary;
                values[base + offset + length / 2u].real = even.real - product_real;
                values[base + offset + length / 2u].imaginary = even.imaginary - product_imaginary;
                {
                    double next_real = twiddle_real * root_real - twiddle_imaginary * root_imaginary;
                    twiddle_imaginary = twiddle_real * root_imaginary + twiddle_imaginary * root_real;
                    twiddle_real = next_real;
                }
            }
        }
        if (length == count)
            break;
    }
}

static int decode_iq_component(const uint8_t *iq, size_t component_index, uint8_t sample_format, double full_scale,
                               double *sample)
{
    if (sample_format == SCANNER_DSP_IQ_FORMAT_S8)
        *sample = (double)decode_s8(iq[component_index]) / full_scale;
    else if (sample_format == SCANNER_DSP_IQ_FORMAT_S16_Q11)
        *sample = (double)decode_s16_le(iq + component_index * 2u) / full_scale;
    else
        return 0;
    return 1;
}

int scanner_dsp_welch_psd_dbfs_hz(const uint8_t *iq, size_t iq_size, size_t complex_sample_count, uint8_t sample_format,
                                  uint32_t sample_rate_hz, size_t fft_size, double *output_dbfs_per_hz,
                                  size_t output_capacity, size_t *output_bin_count)
{
    size_t bytes_per_component;
    size_t expected_size;
    size_t hop_size;
    size_t segment_count;
    size_t segment;
    size_t bin;
    double full_scale;
    double window_energy = 0.0;
    double *window;
    double *power_sum;
    ScannerDspComplex *fft_values;

    if (iq == NULL || output_dbfs_per_hz == NULL || output_bin_count == NULL || sample_rate_hz == 0
        || !is_power_of_two(fft_size) || fft_size < SCANNER_DSP_MIN_FFT_SIZE || fft_size > SCANNER_DSP_MAX_FFT_SIZE
        || output_capacity < fft_size || complex_sample_count < fft_size)
        return 0;
    if (sample_format == SCANNER_DSP_IQ_FORMAT_S8)
    {
        bytes_per_component = 1;
        full_scale = 128.0;
    }
    else if (sample_format == SCANNER_DSP_IQ_FORMAT_S16_Q11)
    {
        bytes_per_component = 2;
        full_scale = 2048.0;
    }
    else
        return 0;
    if (complex_sample_count > SIZE_MAX / (2u * bytes_per_component))
        return 0;
    expected_size = complex_sample_count * 2u * bytes_per_component;
    if (iq_size != expected_size)
        return 0;

    hop_size = fft_size / 2u;
    segment_count = 1u + (complex_sample_count - fft_size) / hop_size;
    window = (double *)malloc(fft_size * sizeof(*window));
    power_sum = (double *)calloc(fft_size, sizeof(*power_sum));
    fft_values = (ScannerDspComplex *)malloc(fft_size * sizeof(*fft_values));
    if (window == NULL || power_sum == NULL || fft_values == NULL)
    {
        free(window);
        free(power_sum);
        free(fft_values);
        return 0;
    }
    for (bin = 0; bin < fft_size; ++bin)
    {
        window[bin] = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * (double)bin / (double)fft_size);
        window_energy += window[bin] * window[bin];
    }

    /* Welch segments overlap by 50%; remove each segment's complex DC mean before windowing. */
    for (segment = 0; segment < segment_count; ++segment)
    {
        size_t sample;
        size_t start = segment * hop_size;
        double mean_i = 0.0;
        double mean_q = 0.0;
        for (sample = 0; sample < fft_size; ++sample)
        {
            double i_value;
            double q_value;
            size_t component_index = (start + sample) * 2u;
            (void)decode_iq_component(iq, component_index, sample_format, full_scale, &i_value);
            (void)decode_iq_component(iq, component_index + 1u, sample_format, full_scale, &q_value);
            mean_i += i_value;
            mean_q += q_value;
        }
        mean_i /= (double)fft_size;
        mean_q /= (double)fft_size;
        for (sample = 0; sample < fft_size; ++sample)
        {
            double i_value;
            double q_value;
            size_t component_index = (start + sample) * 2u;
            (void)decode_iq_component(iq, component_index, sample_format, full_scale, &i_value);
            (void)decode_iq_component(iq, component_index + 1u, sample_format, full_scale, &q_value);
            fft_values[sample].real = (i_value - mean_i) * window[sample];
            fft_values[sample].imaginary = (q_value - mean_q) * window[sample];
        }
        fft_forward(fft_values, fft_size);
        for (bin = 0; bin < fft_size; ++bin)
        {
            size_t source_bin = (bin + fft_size / 2u) % fft_size;
            double real = fft_values[source_bin].real;
            double imaginary = fft_values[source_bin].imaginary;
            power_sum[bin] += (real * real + imaginary * imaginary)
                              / ((double)sample_rate_hz * window_energy * (double)segment_count);
        }
    }

    for (bin = 0; bin < fft_size; ++bin)
        output_dbfs_per_hz[bin] = power_sum[bin] > 0.0 ? 10.0 * log10(power_sum[bin]) : -300.0;
    *output_bin_count = fft_size;
    free(fft_values);
    free(power_sum);
    free(window);
    return 1;
}

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

int scanner_dsp_estimate_noise_floor_dbfs(const uint8_t *iq, size_t iq_size, size_t complex_sample_count,
                                          uint8_t sample_format, int16_t *noise_floor_cdbfs)
{
    size_t bytes_per_component;
    size_t expected_size;
    size_t sample_index;
    long double sum_squared = 0.0L;
    long double full_scale;
    long double normalized_power;
    long double dbfs;

    if (iq == NULL || noise_floor_cdbfs == NULL || complex_sample_count == 0)
        return 0;
    if (sample_format == SCANNER_DSP_IQ_FORMAT_S8)
    {
        bytes_per_component = 1;
        full_scale = 128.0L;
    }
    else if (sample_format == SCANNER_DSP_IQ_FORMAT_S16_Q11)
    {
        bytes_per_component = 2;
        full_scale = 2048.0L;
    }
    else
        return 0;

    if (complex_sample_count > SIZE_MAX / (2u * bytes_per_component))
        return 0;
    expected_size = complex_sample_count * 2u * bytes_per_component;
    if (iq_size != expected_size)
        return 0;

    /* Accumulate both components in a wider type before normalizing to full scale. */
    for (sample_index = 0; sample_index < complex_sample_count; ++sample_index)
    {
        size_t offset = sample_index * 2u * bytes_per_component;
        int32_t i_value;
        int32_t q_value;
        if (sample_format == SCANNER_DSP_IQ_FORMAT_S8)
        {
            i_value = decode_s8(iq[offset]);
            q_value = decode_s8(iq[offset + 1u]);
        }
        else
        {
            i_value = decode_s16_le(iq + offset);
            q_value = decode_s16_le(iq + offset + 2u);
        }
        sum_squared += (long double)i_value * i_value + (long double)q_value * q_value;
    }

    normalized_power = sum_squared / ((long double)complex_sample_count * 2.0L * full_scale * full_scale);
    dbfs = normalized_power <= 0.0L ? -327.68L : 10.0L * log10l(normalized_power);
    if (dbfs < -327.68L)
        dbfs = -327.68L;
    if (dbfs > 327.67L)
        dbfs = 327.67L;
    *noise_floor_cdbfs = (int16_t)llroundl(dbfs * 100.0L);
    return 1;
}
