#include "analysis_worker.h"
#include "perf_probe.h"
#include "perf_probe_hal.h"
#include "radio_frontend.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BENCH_SAMPLE_RATE_HZ 2000000u
#define BENCH_BANDWIDTH_HZ 1750000u
#define BENCH_COMPLEX_SAMPLES 4096u
#define BENCH_FFT_SIZE 1024u
#define BENCH_ANALYSIS_THRESHOLD_DB 8.0
#define BENCH_MIN_BAND_BINS 2u

typedef struct
{
    ScannerRadioBackend backend;
    const char *name;
    uint32_t perf_id;
} BenchBackend;

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;
    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return 0;
    parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > UINT32_MAX)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int find_backend(const ScannerRadioInventory *inventory, ScannerRadioBackend backend)
{
    size_t index;
    for (index = 0; index < inventory->count; ++index)
    {
        if (inventory->frontends[index].backend == backend)
            return inventory->frontends[index].id;
    }
    return -1;
}

static uint8_t to_dsp_format(uint8_t radio_format)
{
    if (radio_format == SCANNER_RADIO_IQ_FORMAT_S8)
        return SCANNER_DSP_IQ_FORMAT_S8;
    if (radio_format == SCANNER_RADIO_IQ_FORMAT_S16)
        return SCANNER_DSP_IQ_FORMAT_S16_Q11;
    return 0;
}

static int run_backend_sweep(const BenchBackend *backend, uint32_t start_mhz, uint32_t stop_mhz, uint32_t step_mhz)
{
    ScannerRadioInventory inventory;
    ScannerAnalysisWorker *analysis_worker = NULL;
    ScannerAnalysisResult analysis_result;
    uint8_t iq[BENCH_COMPLEX_SAMPLES * 4u];
    int frontend_id;
    uint32_t actual_sample_rate = 0;
    uint32_t actual_bandwidth = 0;
    uint64_t requested_hz;
    uint64_t start_us;
    uint64_t end_us;
    uint32_t points = 0;
    uint32_t failed_points = 0;
    char error[256];
    char total_probe_name[CONFIG_PERF_NAME_LEN];
    char point_probe_name[CONFIG_PERF_NAME_LEN];
    int success = 0;
    perf_scope_t total_scope;

    scanner_radio_discover(&inventory);
    frontend_id = find_backend(&inventory, backend->backend);
    if (frontend_id < 0)
    {
        fprintf(stderr, "%s backend was not discovered\n", backend->name);
        scanner_radio_close_all(&inventory);
        return 0;
    }
    if (!scanner_radio_select(&inventory, (uint8_t)frontend_id))
    {
        fprintf(stderr, "Could not open %s frontend %d\n", backend->name, frontend_id);
        scanner_radio_close_all(&inventory);
        return 0;
    }
    if (!scanner_radio_set_sample_rate(&inventory, 0, BENCH_SAMPLE_RATE_HZ, &actual_sample_rate)
        || !scanner_radio_set_bandwidth(&inventory, 0, BENCH_BANDWIDTH_HZ, &actual_bandwidth))
    {
        fprintf(stderr, "Could not configure %s at %u sample rate / %u bandwidth\n", backend->name,
                BENCH_SAMPLE_RATE_HZ, BENCH_BANDWIDTH_HZ);
        scanner_radio_close_all(&inventory);
        return 0;
    }
    if (!scanner_analysis_worker_start(&analysis_worker, error, sizeof(error)))
    {
        fprintf(stderr, "Could not start analysis worker: %s\n", error);
        scanner_radio_close_all(&inventory);
        return 0;
    }

    printf("Benchmark %s: %u..%u MHz step=%u MHz, sample=%u Hz, bandwidth=%u Hz, points=%u\n", backend->name, start_mhz,
           stop_mhz, step_mhz, actual_sample_rate, actual_bandwidth, (stop_mhz - start_mhz) / step_mhz + 1u);
    snprintf(total_probe_name, sizeof(total_probe_name), "sweep.%s.total", backend->name);
    snprintf(point_probe_name, sizeof(point_probe_name), "sweep.%s.point", backend->name);
    perf_reset_all();
    perf_scope_begin(&total_scope, total_probe_name, backend->perf_id);
    start_us = perf_hal_now_us();
    if (backend->backend == SCANNER_RADIO_BACKEND_BLADERF && !scanner_radio_prepare_capture_buffer(&inventory, 0))
    {
        perf_scope_fail(&total_scope);
        perf_scope_end(&total_scope);
        fprintf(stderr, "Could not prepare BladeRF capture buffer\n");
        scanner_analysis_worker_stop(&analysis_worker);
        scanner_radio_close_all(&inventory);
        return 0;
    }
    for (requested_hz = (uint64_t)start_mhz * 1000000u; requested_hz <= (uint64_t)stop_mhz * 1000000u;
         requested_hz += (uint64_t)step_mhz * 1000000u)
    {
        uint8_t radio_format = 0;
        size_t iq_size = 0;
        uint8_t dsp_format;
        perf_scope_t point_scope;
        perf_scope_begin(&point_scope, point_probe_name, backend->perf_id + 100u);
        if (!scanner_radio_set_frequency_no_readback(&inventory, 0, requested_hz)
            || !scanner_radio_capture_iq(&inventory, 0, BENCH_COMPLEX_SAMPLES, iq, sizeof(iq), &iq_size, &radio_format,
                                         2000))
        {
            perf_scope_fail(&point_scope);
            perf_scope_end(&point_scope);
            fprintf(stderr, "%s tune/capture failed at %.3f MHz\n", backend->name, (double)requested_hz / 1000000.0);
            failed_points++;
            break;
        }
        dsp_format = to_dsp_format(radio_format);
        if (dsp_format == 0
            || !scanner_analysis_worker_analyze(analysis_worker, iq, iq_size, BENCH_COMPLEX_SAMPLES, dsp_format,
                                                actual_sample_rate, BENCH_FFT_SIZE, BENCH_ANALYSIS_THRESHOLD_DB,
                                                BENCH_MIN_BAND_BINS, &analysis_result, error, sizeof(error)))
        {
            perf_scope_fail(&point_scope);
            perf_scope_end(&point_scope);
            fprintf(stderr, "%s DSP analysis failed at %.3f MHz: %s\n", backend->name, (double)requested_hz / 1000000.0,
                    error);
            failed_points++;
            break;
        }
        perf_scope_end(&point_scope);
        points++;
        if (points % 50u == 0u)
            printf("%s: %u points complete\n", backend->name, points);
    }
    end_us = perf_hal_now_us();
    if (failed_points != 0u)
        perf_scope_fail(&total_scope);
    perf_scope_end(&total_scope);
    printf("SWEEP_TIME backend=%s points=%u failed=%u total_seconds=%.6f mean_seconds_per_point=%.6f\n", backend->name,
           points, failed_points, (double)(end_us - start_us) / 1000000.0,
           points == 0 ? 0.0 : (double)(end_us - start_us) / (double)points / 1000000.0);
    perf_report_and_reset();
    success = failed_points == 0u && points == (stop_mhz - start_mhz) / step_mhz + 1u;
    scanner_analysis_worker_stop(&analysis_worker);
    scanner_radio_close_all(&inventory);
    return success;
}

int main(int argc, char **argv)
{
    static const BenchBackend backends[] = { { SCANNER_RADIO_BACKEND_HACKRF, "HackRF", 1u },
                                             { SCANNER_RADIO_BACKEND_BLADERF, "BladeRF", 2u } };
    uint32_t start_mhz = 1000u;
    uint32_t stop_mhz = 6000u;
    uint32_t step_mhz = 10u;
    size_t index;
    int result = 0;

    if (argc > 1 && (!parse_u32(argv[1], &start_mhz) || argc > 4))
    {
        fprintf(stderr, "Usage: %s [start_mhz [stop_mhz [step_mhz]]]\n", argv[0]);
        return 2;
    }
    if ((argc > 2 && !parse_u32(argv[2], &stop_mhz)) || (argc > 3 && !parse_u32(argv[3], &step_mhz)) || start_mhz < 70u
        || stop_mhz > 6000u || start_mhz >= stop_mhz || step_mhz == 0u || (stop_mhz - start_mhz) % step_mhz != 0u)
    {
        fprintf(stderr, "Invalid benchmark range or step\n");
        return 2;
    }

    perf_init();
    for (index = 0; index < sizeof(backends) / sizeof(backends[0]); ++index)
    {
        if (!run_backend_sweep(&backends[index], start_mhz, stop_mhz, step_mhz))
            result = 1;
    }
    return result;
}
