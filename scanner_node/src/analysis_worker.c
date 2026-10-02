#include "analysis_worker.h"
#include "signal_classifier.h"
#include "worker_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
typedef HANDLE AnalysisThread;
#else
typedef pthread_t AnalysisThread;
#endif

typedef struct
{
    uint64_t job_id;
    size_t iq_size;
    size_t complex_sample_count;
    uint8_t sample_format;
    uint32_t sample_rate_hz;
    size_t fft_size;
    double detection_threshold_db;
    size_t minimum_band_bins;
    uint8_t iq[SCANNER_ANALYSIS_MAX_IQ_BYTES];
} AnalysisJob;

typedef struct
{
    uint64_t job_id;
    int success;
    ScannerAnalysisResult result;
    char error[160];
} AnalysisCompletion;

struct ScannerAnalysisWorker
{
    ScannerMutex mutex;
    ScannerCondition job_ready;
    ScannerCondition job_space;
    ScannerCondition completion_ready;
    AnalysisThread thread;
    AnalysisJob jobs[SCANNER_ANALYSIS_QUEUE_CAPACITY];
    AnalysisCompletion completions[SCANNER_ANALYSIS_QUEUE_CAPACITY];
    size_t job_head;
    size_t job_count;
    size_t completion_head;
    size_t completion_count;
    uint64_t next_job_id;
    int stopping;
};

static void set_error(char *error, size_t error_size, const char *message, int code)
{
    if (error == NULL || error_size == 0)
        return;
#ifdef _WIN32
    snprintf(error, error_size, "%s (Windows error %d)", message, code);
#else
    snprintf(error, error_size, "%s: %s", message, strerror(code));
#endif
}

static int analyze_job(const AnalysisJob *job, AnalysisCompletion *completion)
{
    double psd[SCANNER_DSP_MAX_FFT_SIZE];
    size_t bin_count = 0;
    ScannerClassifierPlugin generic_plugin = { "generic-spectrum-rules", scanner_classifier_generic, NULL };
    if (!scanner_dsp_estimate_noise_floor_dbfs(job->iq, job->iq_size, job->complex_sample_count, job->sample_format,
                                               &completion->result.mean_window_power_cdbfs))
    {
        snprintf(completion->error, sizeof(completion->error), "%s", "I/Q window power calculation failed");
        return 0;
    }
    if (!scanner_dsp_welch_psd_dbfs_hz(job->iq, job->iq_size, job->complex_sample_count, job->sample_format,
                                       job->sample_rate_hz, job->fft_size, psd, SCANNER_DSP_MAX_FFT_SIZE, &bin_count))
    {
        snprintf(completion->error, sizeof(completion->error), "%s", "Welch PSD calculation failed");
        return 0;
    }
    if (!scanner_dsp_extract_features(psd, bin_count, job->sample_rate_hz, job->detection_threshold_db,
                                      job->minimum_band_bins, &completion->result.features, completion->result.bands,
                                      SCANNER_DSP_MAX_SIGNAL_BANDS))
    {
        snprintf(completion->error, sizeof(completion->error), "%s", "Spectrum feature extraction failed");
        return 0;
    }
    completion->result.band_count = completion->result.features.signal_band_count;
    if (completion->result.band_count > SCANNER_DSP_MAX_SIGNAL_BANDS)
        completion->result.band_count = SCANNER_DSP_MAX_SIGNAL_BANDS;
    if (!scanner_classifier_run(&generic_plugin, 1, &completion->result.features, &completion->result.classification))
    {
        snprintf(completion->error, sizeof(completion->error), "%s", "Signal classification failed");
        return 0;
    }
    return 1;
}

static void analysis_run(ScannerAnalysisWorker *worker)
{
    for (;;)
    {
        AnalysisJob job;
        AnalysisCompletion completion;
        size_t completion_tail;
        scanner_mutex_lock(&worker->mutex);
        while (worker->job_count == 0 && !worker->stopping)
            scanner_condition_wait(&worker->job_ready, &worker->mutex);
        if (worker->stopping && worker->job_count == 0)
        {
            scanner_mutex_unlock(&worker->mutex);
            break;
        }
        job = worker->jobs[worker->job_head];
        worker->job_head = (worker->job_head + 1u) % SCANNER_ANALYSIS_QUEUE_CAPACITY;
        worker->job_count--;
        scanner_condition_signal(&worker->job_space);
        scanner_mutex_unlock(&worker->mutex);

        memset(&completion, 0, sizeof(completion));
        completion.job_id = job.job_id;
        completion.success = analyze_job(&job, &completion);

        scanner_mutex_lock(&worker->mutex);
        while (worker->completion_count == SCANNER_ANALYSIS_QUEUE_CAPACITY && !worker->stopping)
            scanner_condition_wait(&worker->completion_ready, &worker->mutex);
        if (worker->stopping)
        {
            scanner_mutex_unlock(&worker->mutex);
            continue;
        }
        completion_tail = (worker->completion_head + worker->completion_count) % SCANNER_ANALYSIS_QUEUE_CAPACITY;
        worker->completions[completion_tail] = completion;
        worker->completion_count++;
        scanner_condition_broadcast(&worker->completion_ready);
        scanner_mutex_unlock(&worker->mutex);
    }
}

#ifdef _WIN32
static DWORD WINAPI analysis_thread_entry(LPVOID context)
{
    analysis_run((ScannerAnalysisWorker *)context);
    return 0;
}
#else
static void *analysis_thread_entry(void *context)
{
    analysis_run((ScannerAnalysisWorker *)context);
    return NULL;
}
#endif

int scanner_analysis_worker_start(ScannerAnalysisWorker **worker_out, char *error, size_t error_size)
{
    ScannerAnalysisWorker *worker;
#ifndef _WIN32
    int status;
#endif
    if (worker_out == NULL)
        return 0;
    *worker_out = NULL;
    worker = (ScannerAnalysisWorker *)calloc(1, sizeof(*worker));
    if (worker == NULL)
    {
        set_error(error, error_size, "Could not allocate analysis worker", 0);
        return 0;
    }
#ifdef _WIN32
    InitializeCriticalSection(&worker->mutex);
    InitializeConditionVariable(&worker->job_ready);
    InitializeConditionVariable(&worker->job_space);
    InitializeConditionVariable(&worker->completion_ready);
    worker->thread = CreateThread(NULL, 0, analysis_thread_entry, worker, 0, NULL);
    if (worker->thread == NULL)
    {
        set_error(error, error_size, "Could not start analysis worker", (int)GetLastError());
        DeleteCriticalSection(&worker->mutex);
        free(worker);
        return 0;
    }
#else
    status = pthread_mutex_init(&worker->mutex, NULL);
    if (status != 0)
    {
        set_error(error, error_size, "Could not initialize analysis mutex", status);
        free(worker);
        return 0;
    }
    status = pthread_cond_init(&worker->job_ready, NULL);
    if (status != 0)
    {
        set_error(error, error_size, "Could not initialize analysis condition", status);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
    status = pthread_cond_init(&worker->job_space, NULL);
    if (status != 0)
    {
        set_error(error, error_size, "Could not initialize analysis condition", status);
        pthread_cond_destroy(&worker->job_ready);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
    status = pthread_cond_init(&worker->completion_ready, NULL);
    if (status != 0)
    {
        set_error(error, error_size, "Could not initialize analysis condition", status);
        pthread_cond_destroy(&worker->job_space);
        pthread_cond_destroy(&worker->job_ready);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
    status = pthread_create(&worker->thread, NULL, analysis_thread_entry, worker);
    if (status != 0)
    {
        set_error(error, error_size, "Could not start analysis worker", status);
        pthread_cond_destroy(&worker->completion_ready);
        pthread_cond_destroy(&worker->job_space);
        pthread_cond_destroy(&worker->job_ready);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
#endif
    *worker_out = worker;
    return 1;
}

int scanner_analysis_worker_analyze(ScannerAnalysisWorker *worker, const uint8_t *iq, size_t iq_size,
                                    size_t complex_sample_count, uint8_t sample_format, uint32_t sample_rate_hz,
                                    size_t fft_size, double detection_threshold_db, size_t minimum_band_bins,
                                    ScannerAnalysisResult *result, char *error, size_t error_size)
{
    AnalysisJob job;
    uint64_t job_id;
    size_t tail;
    size_t scan;
    if (worker == NULL || iq == NULL || result == NULL || iq_size > SCANNER_ANALYSIS_MAX_IQ_BYTES)
        return 0;
    memset(&job, 0, sizeof(job));
    job.iq_size = iq_size;
    job.complex_sample_count = complex_sample_count;
    job.sample_format = sample_format;
    job.sample_rate_hz = sample_rate_hz;
    job.fft_size = fft_size;
    job.detection_threshold_db = detection_threshold_db;
    job.minimum_band_bins = minimum_band_bins;
    memcpy(job.iq, iq, iq_size);

    scanner_mutex_lock(&worker->mutex);
    while (worker->job_count == SCANNER_ANALYSIS_QUEUE_CAPACITY && !worker->stopping)
        scanner_condition_wait(&worker->job_space, &worker->mutex);
    if (worker->stopping)
    {
        scanner_mutex_unlock(&worker->mutex);
        return 0;
    }
    job_id = ++worker->next_job_id;
    job.job_id = job_id;
    tail = (worker->job_head + worker->job_count) % SCANNER_ANALYSIS_QUEUE_CAPACITY;
    worker->jobs[tail] = job;
    worker->job_count++;
    scanner_condition_signal(&worker->job_ready);

    for (;;)
    {
        for (scan = 0; scan < worker->completion_count; ++scan)
        {
            size_t index = (worker->completion_head + scan) % SCANNER_ANALYSIS_QUEUE_CAPACITY;
            if (worker->completions[index].job_id == job_id)
            {
                AnalysisCompletion completion = worker->completions[index];
                size_t move;
                for (move = scan; move + 1u < worker->completion_count; ++move)
                {
                    size_t from = (worker->completion_head + move + 1u) % SCANNER_ANALYSIS_QUEUE_CAPACITY;
                    size_t to = (worker->completion_head + move) % SCANNER_ANALYSIS_QUEUE_CAPACITY;
                    worker->completions[to] = worker->completions[from];
                }
                worker->completion_count--;
                scanner_condition_signal(&worker->completion_ready);
                scanner_mutex_unlock(&worker->mutex);
                if (!completion.success)
                {
                    if (error != NULL && error_size > 0)
                        snprintf(error, error_size, "%s", completion.error);
                    return 0;
                }
                *result = completion.result;
                return 1;
            }
        }
        if (worker->stopping)
        {
            scanner_mutex_unlock(&worker->mutex);
            return 0;
        }
        scanner_condition_wait(&worker->completion_ready, &worker->mutex);
    }
}

void scanner_analysis_worker_stop(ScannerAnalysisWorker **worker_pointer)
{
    ScannerAnalysisWorker *worker;
    if (worker_pointer == NULL || *worker_pointer == NULL)
        return;
    worker = *worker_pointer;
    scanner_mutex_lock(&worker->mutex);
    worker->stopping = 1;
    scanner_condition_broadcast(&worker->job_ready);
    scanner_condition_broadcast(&worker->job_space);
    scanner_condition_broadcast(&worker->completion_ready);
    scanner_mutex_unlock(&worker->mutex);
#ifdef _WIN32
    WaitForSingleObject(worker->thread, INFINITE);
    CloseHandle(worker->thread);
    DeleteCriticalSection(&worker->mutex);
#else
    (void)pthread_join(worker->thread, NULL);
    pthread_cond_destroy(&worker->completion_ready);
    pthread_cond_destroy(&worker->job_space);
    pthread_cond_destroy(&worker->job_ready);
    pthread_mutex_destroy(&worker->mutex);
#endif
    free(worker);
    *worker_pointer = NULL;
}
