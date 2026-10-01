#include "dsp.h"

#include <limits.h>
#include <math.h>

static int32_t decode_s8(uint8_t value) { return value <= INT8_MAX ? (int32_t)value : (int32_t)value - 256; }

static int32_t decode_s16_le(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    return value <= INT16_MAX ? (int32_t)value : (int32_t)value - 65536;
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
