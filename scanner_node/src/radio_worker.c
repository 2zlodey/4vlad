#include "radio_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION ScannerWorkerMutex;
typedef CONDITION_VARIABLE ScannerWorkerCondition;
typedef HANDLE ScannerWorkerThread;
#else
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

typedef pthread_mutex_t ScannerWorkerMutex;
typedef pthread_cond_t ScannerWorkerCondition;
typedef pthread_t ScannerWorkerThread;
#endif

struct ScannerRadioWorker
{
    ScannerRadioInventory *inventory;
    ScannerRadioWorkerHandler handler;
    void *context;
    ScannerWorkerMutex mutex;
    ScannerWorkerCondition request_ready;
    ScannerWorkerCondition response_space;
    ScannerWorkerThread thread;
    ScannerDatagram requests[SCANNER_RADIO_WORKER_QUEUE_CAPACITY];
    ScannerRadioWorkerResult responses[SCANNER_RADIO_WORKER_QUEUE_CAPACITY];
    size_t request_head;
    size_t request_count;
    size_t response_head;
    size_t response_count;
    int stopping;
    int current_active;
    int cancel_current;
};

static void worker_lock(ScannerRadioWorker *worker)
{
#ifdef _WIN32
    EnterCriticalSection(&worker->mutex);
#else
    (void)pthread_mutex_lock(&worker->mutex);
#endif
}

static void worker_unlock(ScannerRadioWorker *worker)
{
#ifdef _WIN32
    LeaveCriticalSection(&worker->mutex);
#else
    (void)pthread_mutex_unlock(&worker->mutex);
#endif
}

static void worker_wait(ScannerRadioWorker *worker, ScannerWorkerCondition *condition)
{
#ifdef _WIN32
    (void)SleepConditionVariableCS(condition, &worker->mutex, INFINITE);
#else
    (void)pthread_cond_wait(condition, &worker->mutex);
#endif
}

static void worker_signal(ScannerWorkerCondition *condition)
{
#ifdef _WIN32
    WakeConditionVariable(condition);
#else
    (void)pthread_cond_signal(condition);
#endif
}

static void worker_broadcast(ScannerWorkerCondition *condition)
{
#ifdef _WIN32
    WakeAllConditionVariable(condition);
#else
    (void)pthread_cond_broadcast(condition);
#endif
}

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

static int publish_result(ScannerRadioWorker *worker, const ScannerRadioWorkerResult *result)
{
    size_t tail;
    worker_lock(worker);
    while (worker->response_count == SCANNER_RADIO_WORKER_QUEUE_CAPACITY && !worker->stopping)
        worker_wait(worker, &worker->response_space);
    if (worker->stopping)
    {
        worker_unlock(worker);
        return 0;
    }
    tail = (worker->response_head + worker->response_count) % SCANNER_RADIO_WORKER_QUEUE_CAPACITY;
    worker->responses[tail] = *result;
    worker->response_count++;
    worker_unlock(worker);
    return 1;
}

static void worker_run(ScannerRadioWorker *worker)
{
    for (;;)
    {
        ScannerDatagram request;
        ScannerRadioWorkerResult result;
        size_t request_index;
        int action;

        worker_lock(worker);
        while (worker->request_count == 0 && !worker->stopping)
            worker_wait(worker, &worker->request_ready);
        if (worker->stopping)
        {
            worker_unlock(worker);
            break;
        }
        request_index = worker->request_head;
        request = worker->requests[request_index];
        worker->request_head = (request_index + 1u) % SCANNER_RADIO_WORKER_QUEUE_CAPACITY;
        worker->request_count--;
        worker->current_active = 1;
        worker->cancel_current = 0;
        worker_unlock(worker);

        memset(&result, 0, sizeof(result));
        action = worker->handler(worker, worker->inventory, &request, &result, worker->context);
        if (action == 0)
        {
            result.fatal = 1;
            if (result.error[0] == '\0')
                snprintf(result.error, sizeof(result.error), "%s", "Radio command handler failed");
        }
        if (result.size > sizeof(result.payload))
        {
            result.size = 0;
            result.fatal = 1;
            snprintf(result.error, sizeof(result.error), "%s", "Radio worker response exceeded its buffer");
        }

        if ((result.size > 0 || result.fatal || result.close_after_send) && !publish_result(worker, &result))
            action = 0;

        worker_lock(worker);
        worker->current_active = 0;
        worker->cancel_current = 0;
        worker_unlock(worker);
        if (action != 1 || result.fatal || result.close_after_send)
            break;
    }

    scanner_radio_close_all(worker->inventory);
}

#ifdef _WIN32
static DWORD WINAPI worker_thread_entry(LPVOID context)
{
    worker_run((ScannerRadioWorker *)context);
    return 0;
}
#else
static void *worker_thread_entry(void *context)
{
    worker_run((ScannerRadioWorker *)context);
    return NULL;
}
#endif

int scanner_radio_worker_start(ScannerRadioWorker **worker_out, ScannerRadioInventory *inventory,
                               ScannerRadioWorkerHandler handler, void *context, char *error, size_t error_size)
{
    ScannerRadioWorker *worker;
#ifndef _WIN32
    int result;
#endif
    if (worker_out == NULL || inventory == NULL || handler == NULL)
        return 0;
    *worker_out = NULL;
    worker = (ScannerRadioWorker *)calloc(1, sizeof(*worker));
    if (worker == NULL)
    {
        set_error(error, error_size, "Could not allocate radio worker", 0);
        return 0;
    }
    worker->inventory = inventory;
    worker->handler = handler;
    worker->context = context;
#ifdef _WIN32
    InitializeCriticalSection(&worker->mutex);
    InitializeConditionVariable(&worker->request_ready);
    InitializeConditionVariable(&worker->response_space);
    worker->thread = CreateThread(NULL, 0, worker_thread_entry, worker, 0, NULL);
    if (worker->thread == NULL)
    {
        set_error(error, error_size, "Could not start radio worker", (int)GetLastError());
        DeleteCriticalSection(&worker->mutex);
        free(worker);
        return 0;
    }
#else
    result = pthread_mutex_init(&worker->mutex, NULL);
    if (result != 0)
    {
        set_error(error, error_size, "Could not initialize radio worker mutex", result);
        free(worker);
        return 0;
    }
    result = pthread_cond_init(&worker->request_ready, NULL);
    if (result != 0)
    {
        set_error(error, error_size, "Could not initialize radio worker condition", result);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
    result = pthread_cond_init(&worker->response_space, NULL);
    if (result != 0)
    {
        set_error(error, error_size, "Could not initialize radio worker condition", result);
        pthread_cond_destroy(&worker->request_ready);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
    result = pthread_create(&worker->thread, NULL, worker_thread_entry, worker);
    if (result != 0)
    {
        set_error(error, error_size, "Could not start radio worker", result);
        pthread_cond_destroy(&worker->response_space);
        pthread_cond_destroy(&worker->request_ready);
        pthread_mutex_destroy(&worker->mutex);
        free(worker);
        return 0;
    }
#endif
    *worker_out = worker;
    return 1;
}

int scanner_radio_worker_submit(ScannerRadioWorker *worker, const ScannerDatagram *request)
{
    size_t tail;
    if (worker == NULL || request == NULL || request->size > sizeof(request->payload))
        return 0;
    worker_lock(worker);
    if (worker->stopping || worker->request_count == SCANNER_RADIO_WORKER_QUEUE_CAPACITY)
    {
        worker_unlock(worker);
        return 0;
    }
    tail = (worker->request_head + worker->request_count) % SCANNER_RADIO_WORKER_QUEUE_CAPACITY;
    worker->requests[tail] = *request;
    worker->request_count++;
    worker_signal(&worker->request_ready);
    worker_unlock(worker);
    return 1;
}

int scanner_radio_worker_receive(ScannerRadioWorker *worker, ScannerRadioWorkerResult *result)
{
    if (worker == NULL || result == NULL)
        return 0;
    worker_lock(worker);
    if (worker->response_count == 0)
    {
        worker_unlock(worker);
        return 0;
    }
    *result = worker->responses[worker->response_head];
    worker->response_head = (worker->response_head + 1u) % SCANNER_RADIO_WORKER_QUEUE_CAPACITY;
    worker->response_count--;
    worker_signal(&worker->response_space);
    worker_unlock(worker);
    return 1;
}

void scanner_radio_worker_cancel_for_exit(ScannerRadioWorker *worker)
{
    if (worker == NULL)
        return;
    worker_lock(worker);
    worker->request_head = 0;
    worker->request_count = 0;
    worker->cancel_current = 1;
    worker_unlock(worker);
}

int scanner_radio_worker_current_cancelled(ScannerRadioWorker *worker)
{
    int cancelled;
    if (worker == NULL)
        return 1;
    worker_lock(worker);
    cancelled = worker->stopping || worker->cancel_current;
    worker_unlock(worker);
    return cancelled;
}

void scanner_radio_worker_stop(ScannerRadioWorker **worker_pointer)
{
    ScannerRadioWorker *worker;
    if (worker_pointer == NULL || *worker_pointer == NULL)
        return;
    worker = *worker_pointer;
    worker_lock(worker);
    worker->stopping = 1;
    worker->cancel_current = 1;
    worker_broadcast(&worker->request_ready);
    worker_broadcast(&worker->response_space);
    worker_unlock(worker);
#ifdef _WIN32
    WaitForSingleObject(worker->thread, INFINITE);
    CloseHandle(worker->thread);
    DeleteCriticalSection(&worker->mutex);
#else
    (void)pthread_join(worker->thread, NULL);
    pthread_cond_destroy(&worker->response_space);
    pthread_cond_destroy(&worker->request_ready);
    pthread_mutex_destroy(&worker->mutex);
#endif
    free(worker);
    *worker_pointer = NULL;
}