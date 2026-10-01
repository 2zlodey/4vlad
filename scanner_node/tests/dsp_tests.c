#include "dsp.h"

#include <stdio.h>

static int require_equal(int actual, int expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", message, actual, expected);
        return 0;
    }
    return 1;
}

static int require_true(int condition, const char *message)
{
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

int main(void)
{
    static const uint8_t s8_quarter_power[] = { 0x40, 0x40, 0x40, 0x40 };
    static const uint8_t s8_full_scale[] = { 0x80, 0x80, 0x80, 0x80 };
    static const uint8_t s8_silence[] = { 0, 0, 0, 0 };
    static const uint8_t s16_q11_quarter_power[] = { 0x00, 0x04, 0x00, 0x04, 0x00, 0xfc, 0x00, 0xfc };
    int16_t result = 0;
    int ok = 1;

    ok &= require_true(scanner_dsp_estimate_noise_floor_dbfs(s8_quarter_power, sizeof(s8_quarter_power), 2,
                                                             SCANNER_DSP_IQ_FORMAT_S8, &result),
                       "S8 window was rejected");
    ok &= require_equal(result, -602, "S8 normalized power is not -6.02 dBFS");

    ok &= require_true(scanner_dsp_estimate_noise_floor_dbfs(s16_q11_quarter_power, sizeof(s16_q11_quarter_power), 2,
                                                             SCANNER_DSP_IQ_FORMAT_S16_Q11, &result),
                       "S16 Q11 window was rejected");
    ok &= require_equal(result, -602, "S16 Q11 normalized power is not -6.02 dBFS");

    ok &= require_true(scanner_dsp_estimate_noise_floor_dbfs(s8_full_scale, sizeof(s8_full_scale), 2,
                                                             SCANNER_DSP_IQ_FORMAT_S8, &result),
                       "Full-scale S8 window was rejected");
    ok &= require_equal(result, 0, "Full-scale S8 window did not normalize to 0 dBFS");

    ok &= require_true(scanner_dsp_estimate_noise_floor_dbfs(s8_silence, sizeof(s8_silence), 2,
                                                             SCANNER_DSP_IQ_FORMAT_S8, &result),
                       "Zero S8 window was rejected");
    ok &= require_equal(result, INT16_MIN, "Zero-power floor did not clamp to int16 minimum");

    ok &= require_true(!scanner_dsp_estimate_noise_floor_dbfs(s8_quarter_power, sizeof(s8_quarter_power) - 1u, 2,
                                                              SCANNER_DSP_IQ_FORMAT_S8, &result),
                       "Mismatched I/Q byte count was accepted");
    ok &= require_true(!scanner_dsp_estimate_noise_floor_dbfs(s8_quarter_power, sizeof(s8_quarter_power), 2, 0xffu,
                                                              &result),
                       "Unknown I/Q format was accepted");
    ok &= require_true(!scanner_dsp_estimate_noise_floor_dbfs(NULL, 0, 0, SCANNER_DSP_IQ_FORMAT_S8, &result),
                       "Null/empty I/Q input was accepted");

    if (ok)
        puts("DSP noise-floor tests passed.");
    return ok ? 0 : 1;
}
