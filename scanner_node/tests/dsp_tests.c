#include "dsp.h"
#include "signal_classifier.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int require_near(double actual, double expected, double tolerance, const char *message)
{
    if (!isfinite(actual) || fabs(actual - expected) > tolerance)
    {
        fprintf(stderr, "FAIL: %s (got %.6f, expected %.6f +/- %.6f)\n", message, actual, expected, tolerance);
        return 0;
    }
    return 1;
}

static int run_generic_classifier(const ScannerDspFeatures *features, ScannerClassification *result)
{
    ScannerClassifierPlugin plugin = { "generic", scanner_classifier_generic, NULL };
    return scanner_classifier_run(&plugin, 1, features, result);
}

int main(void)
{
    static const uint8_t s8_quarter_power[] = { 0x40, 0x40, 0x40, 0x40 };
    static const uint8_t s8_full_scale[] = { 0x80, 0x80, 0x80, 0x80 };
    static const uint8_t s8_silence[] = { 0, 0, 0, 0 };
    static const uint8_t s16_q11_quarter_power[] = { 0x00, 0x04, 0x00, 0x04, 0x00, 0xfc, 0x00, 0xfc };
    uint8_t tone_iq[1024u * 2u];
    double psd[256];
    double cfar_psd[128];
    uint8_t cfar_active[128];
    double noise_psd[2048];
    uint8_t noise_active[2048];
    size_t psd_bins = 0;
    size_t sample;
    size_t false_alarms = 0;
    ScannerDspFeatures features;
    ScannerDspSignalBand bands[SCANNER_DSP_MAX_SIGNAL_BANDS];
    ScannerClassification classification;
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

    /* Four coherent Welch segments contain the same positive-offset complex tone. */
    for (sample = 0; sample < 1024; ++sample)
    {
        double phase = 2.0 * 3.14159265358979323846 * 8.0 * (double)sample / 256.0;
        int8_t i_value = (int8_t)lrint(80.0 * cos(phase));
        int8_t q_value = (int8_t)lrint(80.0 * sin(phase));
        tone_iq[sample * 2u] = (uint8_t)i_value;
        tone_iq[sample * 2u + 1u] = (uint8_t)q_value;
    }
    ok &= require_true(scanner_dsp_welch_psd_dbfs_hz(tone_iq, sizeof(tone_iq), 1024, SCANNER_DSP_IQ_FORMAT_S8, 256000,
                                                     256, psd, 256, &psd_bins),
                       "Welch PSD rejected a valid S8 tone buffer");
    ok &= require_equal((int)psd_bins, 256, "Welch PSD returned the wrong number of bins");
    ok &= require_true(scanner_dsp_extract_features(psd, psd_bins, 256000, 10.0, 2, &features, bands,
                                                    SCANNER_DSP_MAX_SIGNAL_BANDS),
                       "Feature extraction rejected a valid PSD");
    fprintf(stderr,
            "DSP tone features: peak_offset=%.1fHz peak_floor=%.2fdB occupied=%.1fHz fraction=%.4f bands=%lu "
            "flatness=%.4f\n",
            features.peak_offset_hz, features.peak_above_floor_db, features.occupied_bandwidth_hz,
            features.occupied_fraction, (unsigned long)features.signal_band_count, features.spectral_flatness);
    ok &= require_near(features.peak_offset_hz, 8000.0, 1000.0, "Complex tone peak offset is incorrect");
    ok &= require_true(features.signal_band_count >= 1 && features.occupied_bandwidth_hz > 0.0,
                       "Tone did not produce an occupied signal band");
    ok &= require_true(run_generic_classifier(&features, &classification)
                           && classification.signal_class == SCANNER_SIGNAL_NARROWBAND_TONE,
                       "Generic classifier did not recognize a strong narrowband tone");
    {
        ScannerDspFeatures ambiguous;
        memset(&ambiguous, 0, sizeof(ambiguous));
        ok &= require_true(run_generic_classifier(&ambiguous, &classification)
                               && classification.signal_class == SCANNER_SIGNAL_UNKNOWN,
                           "Generic classifier must return UNKNOWN when features are insufficient");
    }
    ok &= require_true(!scanner_dsp_welch_psd_dbfs_hz(tone_iq, sizeof(tone_iq) - 1u, 1024, SCANNER_DSP_IQ_FORMAT_S8,
                                                      256000, 256, psd, 256, &psd_bins),
                       "Welch PSD accepted a mismatched byte count");
    ok &= require_true(!scanner_dsp_welch_psd_dbfs_hz(tone_iq, sizeof(tone_iq), 1024, SCANNER_DSP_IQ_FORMAT_S8, 256000,
                                                      255, psd, 256, &psd_bins),
                       "Welch PSD accepted a non-power-of-two FFT size");

    for (sample = 0; sample < 128; ++sample)
        cfar_psd[sample] = -100.0 + 0.3 * sample;
    for (sample = 62; sample <= 65; ++sample)
        cfar_psd[sample] += 18.0;
    ok &= require_true(scanner_dsp_cfar_detect(cfar_psd, 128, 12, 2, 1e-3, cfar_active, 128),
                       "CA-CFAR rejected a valid PSD");
    ok &= require_true(cfar_active[63] && cfar_active[64] && !cfar_active[30] && !cfar_active[100],
                       "CA-CFAR failed to detect a signal on a sloped noise floor");
    ok &= require_true(!scanner_dsp_cfar_detect(cfar_psd, 128, 0, 2, 1e-3, cfar_active, 128),
                       "CA-CFAR accepted zero training bins");
    {
        uint32_t random_state = UINT32_C(0x13579bdf);
        for (sample = 0; sample < 2048; ++sample)
        {
            double uniform;
            random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
            uniform = ((double)random_state + 1.0) / 4294967297.0;
            noise_psd[sample] = 10.0 * log10(-log(uniform));
        }
        ok &= require_true(scanner_dsp_cfar_detect(noise_psd, 2048, 12, 2, 1e-3, noise_active, 2048),
                           "CA-CFAR rejected a noise-only spectrum");
        for (sample = 0; sample < 2048; ++sample)
            false_alarms += noise_active[sample] != 0;
        ok &= require_true(false_alarms <= 12, "CA-CFAR produced excessive alarms on exponential noise bins");
    }

    if (ok)
        puts("DSP noise-floor, PSD, feature, and classifier tests passed.");
    return ok ? 0 : 1;
}
