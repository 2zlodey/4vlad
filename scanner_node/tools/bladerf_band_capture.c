#include "dsp.h"
#include "radio_frontend.h"
#include "signal_classifier.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#define CAPTURE_PAIRS 16384u
#define PSD_FFT_SIZE 4096u
#define SAMPLE_RATE_HZ 20000000u
#define RX_BANDWIDTH_HZ 15000000u
#define HALF_SPAN_HZ UINT64_C(40000000)
#define DETECTION_THRESHOLD_DB 8.0
#define MINIMUM_BAND_BINS 2u
#define WARMUP_SPAN_HZ UINT64_C(5000000)
#define DC_GUARD_HZ 50000u
#define HARMONIC_COUNT 3u
#define POINTS_PER_HARMONIC 129u
#define MAX_CANDIDATE_BANDS (HARMONIC_COUNT * POINTS_PER_HARMONIC * SCANNER_DSP_MAX_SIGNAL_BANDS)

typedef struct
{
    unsigned int harmonic;
    double lower_hz;
    double upper_hz;
    double peak_hz;
    double peak_dbfs_per_hz;
    double noise_floor_dbfs_per_hz;
    double peak_above_floor_db;
    ScannerSignalClass signal_class;
    double confidence;
} CandidateBand;

static int compare_bands(const void *left, const void *right)
{
    const CandidateBand *a = (const CandidateBand *)left;
    const CandidateBand *b = (const CandidateBand *)right;
    if (a->harmonic != b->harmonic)
        return a->harmonic < b->harmonic ? -1 : 1;
    if (a->lower_hz < b->lower_hz)
        return -1;
    if (a->lower_hz > b->lower_hz)
        return 1;
    return 0;
}

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static void suppress_center_dc(double *psd, size_t count, uint32_t sample_rate_hz)
{
    double floor_values[PSD_FFT_SIZE];
    size_t floor_count = 0;
    size_t center = count / 2u;
    size_t guard_bins = ((size_t)DC_GUARD_HZ * count + sample_rate_hz - 1u) / sample_rate_hz;
    size_t bin;
    double median_floor;
    for (bin = 0; bin < count; ++bin)
    {
        size_t distance = bin > center ? bin - center : center - bin;
        if (distance > guard_bins)
            floor_values[floor_count++] = psd[bin];
    }
    if (floor_count == 0)
        return;
    qsort(floor_values, floor_count, sizeof(*floor_values), compare_double);
    median_floor = floor_values[floor_count / 2u];
    for (bin = center - guard_bins; bin <= center + guard_bins && bin < count; ++bin)
        psd[bin] = median_floor;
}

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s output.csv [fundamental_mhz [half_span_mhz [step_khz [forward|reverse]]]]\n", program);
}

static void wait_for_tuner_settle(void)
{
#ifdef _WIN32
    Sleep(20);
#else
    struct timespec settle = { 0, 20000000L };
    nanosleep(&settle, NULL);
#endif
}

int main(int argc, char **argv)
{
    static const uint32_t default_fundamental_mhz = 1660u;
    ScannerRadioInventory inventory;
    uint8_t iq[CAPTURE_PAIRS * 4u];
    uint8_t discard_iq[CAPTURE_PAIRS * 4u];
    double psd[PSD_FFT_SIZE];
    CandidateBand *candidates;
    size_t candidate_count = 0;
    unsigned int fundamental_mhz = default_fundamental_mhz;
    unsigned int half_span_mhz = 40u;
    unsigned int step_khz = 10000u;
    unsigned int harmonic;
    int reverse_scan = 0;
    size_t point_index;
    uint32_t applied_sample_rate;
    uint32_t applied_bandwidth;
    int16_t total_gain_cdb = 0;
    int frontend_id = -1;
    FILE *output;
    int ok = 1;

    if (argc < 2 || argc > 6)
    {
        print_usage(argv[0]);
        return 2;
    }
    if (argc > 2)
        fundamental_mhz = (unsigned int)strtoul(argv[2], NULL, 10);
    if (argc > 3)
        half_span_mhz = (unsigned int)strtoul(argv[3], NULL, 10);
    if (argc > 4)
        step_khz = (unsigned int)strtoul(argv[4], NULL, 10);
    if (argc > 5)
    {
        if (strcmp(argv[5], "reverse") == 0)
            reverse_scan = 1;
        else if (strcmp(argv[5], "forward") != 0)
        {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (fundamental_mhz < 100u || fundamental_mhz > 2000u || half_span_mhz == 0u || half_span_mhz > 100u
        || step_khz == 0u || step_khz > 15000u || (half_span_mhz * 2000u) / step_khz + 1u > POINTS_PER_HARMONIC)
    {
        fprintf(stderr, "Invalid scan range or step\n");
        return 2;
    }

    scanner_radio_discover(&inventory);
    for (point_index = 0; point_index < inventory.count; ++point_index)
    {
        if (inventory.frontends[point_index].backend == SCANNER_RADIO_BACKEND_BLADERF)
        {
            frontend_id = inventory.frontends[point_index].id;
            break;
        }
    }
    if (frontend_id < 0)
    {
        fprintf(stderr, "No BladeRF backend found; refusing synthetic fallback data\n");
        scanner_radio_close_all(&inventory);
        return 1;
    }
    if (!scanner_radio_select(&inventory, (uint8_t)frontend_id))
    {
        fprintf(stderr, "Could not open BladeRF frontend %d\n", frontend_id);
        scanner_radio_close_all(&inventory);
        return 1;
    }
    if (!scanner_radio_set_sample_rate(&inventory, 0, SAMPLE_RATE_HZ, &applied_sample_rate)
        || !scanner_radio_set_bandwidth(&inventory, 0, RX_BANDWIDTH_HZ, &applied_bandwidth))
    {
        fprintf(stderr, "Could not configure BladeRF sample rate/bandwidth\n");
        scanner_radio_close_all(&inventory);
        return 1;
    }
    (void)scanner_radio_get_total_gain(&inventory, 0, &total_gain_cdb);

    candidates = (CandidateBand *)calloc(MAX_CANDIDATE_BANDS, sizeof(*candidates));
    if (candidates == NULL)
    {
        scanner_radio_close_all(&inventory);
        return 1;
    }
    printf("BladeRF frontend %d: sample=%u Hz bandwidth=%u Hz gain=%.2f dB\n", frontend_id, applied_sample_rate,
           applied_bandwidth, (double)total_gain_cdb / 100.0);
    printf("Scanning fundamental %u MHz and harmonics through %u MHz; tune step %u kHz (%s)\n", fundamental_mhz,
           fundamental_mhz * HARMONIC_COUNT, step_khz, reverse_scan ? "reverse" : "forward");

    for (harmonic = 1; harmonic <= HARMONIC_COUNT && ok; ++harmonic)
    {
        uint64_t nominal_hz = (uint64_t)fundamental_mhz * harmonic * UINT64_C(1000000);
        uint64_t start_hz = nominal_hz - (uint64_t)half_span_mhz * UINT64_C(1000000);
        uint64_t stop_hz = nominal_hz + (uint64_t)half_span_mhz * UINT64_C(1000000);
        uint64_t step_hz = (uint64_t)step_khz * 1000u;
        size_t point_count = (size_t)((stop_hz - start_hz) / step_hz) + 1u;
        size_t warmup_points = (size_t)((WARMUP_SPAN_HZ + step_hz - 1u) / step_hz);

        for (point_index = 0; point_index < point_count; ++point_index)
        {
            size_t scan_index = reverse_scan ? point_count - 1u - point_index : point_index;
            uint64_t requested_hz = start_hz + scan_index * step_hz;
            uint64_t actual_hz = 0;
            size_t iq_size = 0;
            size_t discard_size = 0;
            size_t psd_count = 0;
            size_t band_index;
            uint8_t sample_format = 0;
            uint8_t discard_format = 0;
            ScannerDspFeatures features;
            ScannerDspSignalBand bands[SCANNER_DSP_MAX_SIGNAL_BANDS];
            ScannerClassification classification;
            ScannerClassifierPlugin plugin = { "generic-spectrum-rules", scanner_classifier_generic, NULL };
            if (!scanner_radio_set_frequency(&inventory, 0, requested_hz, &actual_hz))
            {
                fprintf(stderr, "Tune failed at requested %.6f MHz\n", (double)requested_hz / 1.0e6);
                ok = 0;
                break;
            }
            wait_for_tuner_settle();
            /* Discard the first post-retune block so LO/DC settling cannot define the band edge. */
            if (!scanner_radio_capture_iq(&inventory, 0, CAPTURE_PAIRS, discard_iq, sizeof(discard_iq), &discard_size,
                                          &discard_format, 2000)
                || !scanner_radio_capture_iq(&inventory, 0, CAPTURE_PAIRS, iq, sizeof(iq), &iq_size, &sample_format,
                                             2000))
            {
                fprintf(stderr, "RX capture failed at requested %.6f MHz\n", (double)requested_hz / 1.0e6);
                ok = 0;
                break;
            }
            /* Startup artifacts followed the scan edge in both directions; discard initial tune points. */
            if (point_index < warmup_points)
            {
                if (point_index == 0 || point_index + 1u == warmup_points)
                    printf("harmonic %u: discarding first %.3f MHz for startup settling\n", harmonic,
                           (double)WARMUP_SPAN_HZ / 1.0e6);
                continue;
            }
            if (!scanner_dsp_welch_psd_dbfs_hz(iq, iq_size, CAPTURE_PAIRS,
                                               sample_format == SCANNER_RADIO_IQ_FORMAT_S16
                                                   ? SCANNER_DSP_IQ_FORMAT_S16_Q11
                                                   : SCANNER_DSP_IQ_FORMAT_S8,
                                               applied_sample_rate, PSD_FFT_SIZE, psd, PSD_FFT_SIZE, &psd_count))
            {
                fprintf(stderr, "Welch PSD failed at actual %.6f MHz\n", (double)actual_hz / 1.0e6);
                ok = 0;
                break;
            }
            /* Reject the zero-IF LO/DC spur; neighboring tune windows still cover real carriers here. */
            suppress_center_dc(psd, psd_count, applied_sample_rate);
            if (!scanner_dsp_extract_features(psd, psd_count, applied_sample_rate, DETECTION_THRESHOLD_DB,
                                              MINIMUM_BAND_BINS, &features, bands, SCANNER_DSP_MAX_SIGNAL_BANDS)
                || !scanner_classifier_run(&plugin, 1, &features, &classification))
            {
                fprintf(stderr, "DSP failed at actual %.6f MHz\n", (double)actual_hz / 1.0e6);
                ok = 0;
                break;
            }
            for (band_index = 0; band_index < features.signal_band_count && band_index < SCANNER_DSP_MAX_SIGNAL_BANDS;
                 ++band_index)
            {
                CandidateBand *candidate;
                if (candidate_count >= MAX_CANDIDATE_BANDS)
                {
                    ok = 0;
                    break;
                }
                candidate = &candidates[candidate_count++];
                candidate->harmonic = harmonic;
                candidate->lower_hz = (double)actual_hz + bands[band_index].lower_offset_hz;
                candidate->upper_hz = (double)actual_hz + bands[band_index].upper_offset_hz;
                candidate->peak_hz = (double)actual_hz + bands[band_index].peak_offset_hz;
                candidate->peak_dbfs_per_hz = bands[band_index].peak_dbfs_per_hz;
                candidate->noise_floor_dbfs_per_hz = features.noise_floor_dbfs_per_hz;
                candidate->peak_above_floor_db = bands[band_index].peak_above_floor_db;
                candidate->signal_class = classification.signal_class;
                candidate->confidence = classification.confidence;
            }
            if (((point_index + 1u) % 20u) == 0u || point_index + 1u == point_count)
                printf("harmonic %u: %lu/%lu tune points\n", harmonic, (unsigned long)(point_index + 1u),
                       (unsigned long)point_count);
        }
    }

    scanner_radio_close_all(&inventory);
    if (!ok)
    {
        free(candidates);
        return 1;
    }
    qsort(candidates, candidate_count, sizeof(*candidates), compare_bands);
    output = fopen(argv[1], "w");
    if (output == NULL)
    {
        perror("Could not open output CSV");
        free(candidates);
        return 1;
    }
    fprintf(output,
            "harmonic,nominal_mhz,lower_edge_mhz,upper_edge_mhz,bandwidth_khz,peak_mhz,"
            "peak_dbfs_per_hz,noise_floor_dbfs_per_hz,peak_above_floor_db,class,confidence\n");
    {
        size_t index = 0;
        unsigned int emitted_harmonic = 0;
        while (index < candidate_count)
        {
            CandidateBand merged = candidates[index++];
            while (index < candidate_count && candidates[index].harmonic == merged.harmonic
                   && candidates[index].lower_hz <= merged.upper_hz + 2.0 * applied_sample_rate / PSD_FFT_SIZE)
            {
                CandidateBand *next = &candidates[index++];
                if (next->upper_hz > merged.upper_hz)
                    merged.upper_hz = next->upper_hz;
                if (next->peak_dbfs_per_hz > merged.peak_dbfs_per_hz)
                {
                    merged.peak_hz = next->peak_hz;
                    merged.peak_dbfs_per_hz = next->peak_dbfs_per_hz;
                    merged.peak_above_floor_db = next->peak_above_floor_db;
                    merged.signal_class = next->signal_class;
                    merged.confidence = next->confidence;
                }
                if (next->noise_floor_dbfs_per_hz > merged.noise_floor_dbfs_per_hz)
                    merged.noise_floor_dbfs_per_hz = next->noise_floor_dbfs_per_hz;
            }
            while (emitted_harmonic < merged.harmonic)
            {
                emitted_harmonic++;
                if (emitted_harmonic < merged.harmonic)
                    fprintf(output, "%u,%u,,,,,,,,no-detection,0\n", emitted_harmonic,
                            fundamental_mhz * emitted_harmonic);
            }
            fprintf(output, "%u,%u,%.6f,%.6f,%.3f,%.6f,%.2f,%.2f,%.2f,%s,%.3f\n", merged.harmonic,
                    fundamental_mhz * merged.harmonic, merged.lower_hz / 1.0e6, merged.upper_hz / 1.0e6,
                    (merged.upper_hz - merged.lower_hz) / 1.0e3, merged.peak_hz / 1.0e6, merged.peak_dbfs_per_hz,
                    merged.noise_floor_dbfs_per_hz, merged.peak_above_floor_db,
                    scanner_signal_class_name(merged.signal_class), merged.confidence);
            emitted_harmonic = merged.harmonic;
        }
        while (emitted_harmonic < HARMONIC_COUNT)
        {
            emitted_harmonic++;
            fprintf(output, "%u,%u,,,,,,,,no-detection,0\n", emitted_harmonic, fundamental_mhz * emitted_harmonic);
        }
    }
    if (fclose(output) != 0)
    {
        perror("Could not finish output CSV");
        free(candidates);
        return 1;
    }
    printf("Saved %lu local band detections to %s\n", (unsigned long)candidate_count, argv[1]);
    free(candidates);
    return 0;
}
