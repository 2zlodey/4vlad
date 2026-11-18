#ifndef SCANNER_NODE_WORKER_SYNC_H
#define SCANNER_NODE_WORKER_SYNC_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION ScannerMutex;
typedef CONDITION_VARIABLE ScannerCondition;

static inline void scanner_mutex_lock(ScannerMutex *mutex) { EnterCriticalSection(mutex); }
static inline void scanner_mutex_unlock(ScannerMutex *mutex) { LeaveCriticalSection(mutex); }
static inline void scanner_condition_wait(ScannerCondition *condition, ScannerMutex *mutex)
{
    (void)SleepConditionVariableCS(condition, mutex, INFINITE);
}
static inline int scanner_condition_wait_timeout(ScannerCondition *condition, ScannerMutex *mutex,
                                                 unsigned int timeout_ms)
{
    if (SleepConditionVariableCS(condition, mutex, timeout_ms))
        return 0;
    return GetLastError() == ERROR_TIMEOUT;
}
static inline void scanner_condition_signal(ScannerCondition *condition) { WakeConditionVariable(condition); }
static inline void scanner_condition_broadcast(ScannerCondition *condition) { WakeAllConditionVariable(condition); }
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t ScannerMutex;
typedef pthread_cond_t ScannerCondition;

static inline void scanner_mutex_lock(ScannerMutex *mutex) { (void)pthread_mutex_lock(mutex); }
static inline void scanner_mutex_unlock(ScannerMutex *mutex) { (void)pthread_mutex_unlock(mutex); }
static inline void scanner_condition_wait(ScannerCondition *condition, ScannerMutex *mutex)
{
    (void)pthread_cond_wait(condition, mutex);
}
static inline int scanner_condition_wait_timeout(ScannerCondition *condition, ScannerMutex *mutex,
                                                 unsigned int timeout_ms)
{
    struct timespec deadline;
    int result;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 0;
    deadline.tv_sec += timeout_ms / 1000u;
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    result = pthread_cond_timedwait(condition, mutex, &deadline);
    return result == ETIMEDOUT;
}
static inline void scanner_condition_signal(ScannerCondition *condition) { (void)pthread_cond_signal(condition); }
static inline void scanner_condition_broadcast(ScannerCondition *condition) { (void)pthread_cond_broadcast(condition); }
#endif

#endif