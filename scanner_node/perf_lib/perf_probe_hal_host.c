#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "perf_probe_hal.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>


static void (*log_sink)(const char *line);
static char log_line[1024];
static size_t log_length;

void perf_hal_set_log_sink(void (*sink)(const char *line))
{
    log_sink = sink;
    log_length = 0;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static CRITICAL_SECTION g_perf_mutex;
static LARGE_INTEGER g_perf_frequency;
#else
#include <pthread.h>
#include <time.h>
static pthread_mutex_t g_perf_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

void perf_hal_init(void)
{
#ifdef _WIN32
    InitializeCriticalSection(&g_perf_mutex);
    (void)QueryPerformanceFrequency(&g_perf_frequency);
#endif
}

uint64_t perf_hal_now_us(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    uint64_t frequency;
    if (g_perf_frequency.QuadPart <= 0 || !QueryPerformanceCounter(&counter))
        return 0;
    frequency = (uint64_t)g_perf_frequency.QuadPart;
    return ((uint64_t)counter.QuadPart / frequency) * UINT64_C(1000000)
           + (((uint64_t)counter.QuadPart % frequency) * UINT64_C(1000000)) / frequency;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000) + (uint64_t)now.tv_nsec / 1000u;
#endif
}

uint64_t perf_hal_counter(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter))
        return 0;
    return (uint64_t)counter.QuadPart;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
#endif
}

uint32_t perf_hal_critical_enter(void)
{
#ifdef _WIN32
    EnterCriticalSection(&g_perf_mutex);
#else
    (void)pthread_mutex_lock(&g_perf_mutex);
#endif
    return 0;
}

void perf_hal_critical_exit(uint32_t state)
{
    (void)state;
#ifdef _WIN32
    LeaveCriticalSection(&g_perf_mutex);
#else
    (void)pthread_mutex_unlock(&g_perf_mutex);
#endif
}

void perf_hal_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (log_sink == NULL)
        (void)vprintf(fmt, args);
    else
    {
        char fragment[1024];
        const char *position;
        vsnprintf(fragment, sizeof(fragment), fmt, args);
        for (position = fragment; *position; ++position)
        {
            if (*position == '\r')
                continue;
            if (*position == '\n' || log_length == sizeof(log_line) - 1)
            {
                log_line[log_length] = '\0';
                log_sink(log_line);
                log_length = 0;
            }
            if (*position != '\n')
                log_line[log_length++] = *position;
        }
    }
    va_end(args);
}
