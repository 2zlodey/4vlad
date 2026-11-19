#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "dsp.h"

#include <fftw3.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK fftw_access_lock = SRWLOCK_INIT;
static void fftw_lifecycle_lock_acquire(void) { AcquireSRWLockExclusive(&fftw_access_lock); }
static void fftw_lifecycle_lock_release(void) { ReleaseSRWLockExclusive(&fftw_access_lock); }
static void fftw_execution_lock_acquire(void) { AcquireSRWLockShared(&fftw_access_lock); }
static void fftw_execution_lock_release(void) { ReleaseSRWLockShared(&fftw_access_lock); }
#else
#include <pthread.h>
static pthread_rwlock_t fftw_access_lock = PTHREAD_RWLOCK_INITIALIZER;
static void fftw_lifecycle_lock_acquire(void) { (void)pthread_rwlock_wrlock(&fftw_access_lock); }
static void fftw_lifecycle_lock_release(void) { (void)pthread_rwlock_unlock(&fftw_access_lock); }
static void fftw_execution_lock_acquire(void) { (void)pthread_rwlock_rdlock(&fftw_access_lock); }
static void fftw_execution_lock_release(void) { (void)pthread_rwlock_unlock(&fftw_access_lock); }
#endif

static int32_t decode_s8(uint8_t value) { return value <= INT8_MAX ? (int32_t)value : (int32_t)value - 256; }

static int32_t decode_s16_le(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    return value <= INT16_MAX ? (int32_t)value : (int32_t)value - 65536;
}

static int is_power_of_two(size_t value) { return value != 0 && (value & (value - 1u)) == 0; }

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
    fftw_complex *fft_values;
    fftw_plan fft_plan;

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
    fft_values = (fftw_complex *)fftw_malloc(fft_size * sizeof(*fft_values));
    if (window == NULL || power_sum == NULL || fft_values == NULL)
    {
        free(window);
        free(power_sum);
        fftw_free(fft_values);
        return 0;
    }
    fftw_lifecycle_lock_acquire();
    fft_plan = fftw_plan_dft_1d((int)fft_size, fft_values, fft_values, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_lifecycle_lock_release();
    if (fft_plan == NULL)
    {
        free(window);
        free(power_sum);
        fftw_free(fft_values);
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
            double i_value = 0.0;
            double q_value = 0.0;
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
            double i_value = 0.0;
            double q_value = 0.0;
            size_t component_index = (start + sample) * 2u;
            (void)decode_iq_component(iq, component_index, sample_format, full_scale, &i_value);
            (void)decode_iq_component(iq, component_index + 1u, sample_format, full_scale, &q_value);
            fft_values[sample][0] = (i_value - mean_i) * window[sample];
            fft_values[sample][1] = (q_value - mean_q) * window[sample];
        }
        fftw_execution_lock_acquire();
        fftw_execute(fft_plan);
        fftw_execution_lock_release();
        for (bin = 0; bin < fft_size; ++bin)
        {
            size_t source_bin = (bin + fft_size / 2u) % fft_size;
            double real = fft_values[source_bin][0];
            double imaginary = fft_values[source_bin][1];
            power_sum[bin] += (real * real + imaginary * imaginary)
                              / ((double)sample_rate_hz * window_energy * (double)segment_count);
        }
    }

    for (bin = 0; bin < fft_size; ++bin)
        output_dbfs_per_hz[bin] = power_sum[bin] > 0.0 ? 10.0 * log10(power_sum[bin]) : -300.0;
    *output_bin_count = fft_size;
    fftw_lifecycle_lock_acquire();
    fftw_destroy_plan(fft_plan);
    fftw_lifecycle_lock_release();
    fftw_free(fft_values);
    free(power_sum);
    free(window);
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
