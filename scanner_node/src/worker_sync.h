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
static inline void scanner_condition_signal(ScannerCondition *condition) { WakeConditionVariable(condition); }
static inline void scanner_condition_broadcast(ScannerCondition *condition) { WakeAllConditionVariable(condition); }
#else
#include <pthread.h>
typedef pthread_mutex_t ScannerMutex;
typedef pthread_cond_t ScannerCondition;

static inline void scanner_mutex_lock(ScannerMutex *mutex) { (void)pthread_mutex_lock(mutex); }
static inline void scanner_mutex_unlock(ScannerMutex *mutex) { (void)pthread_mutex_unlock(mutex); }
static inline void scanner_condition_wait(ScannerCondition *condition, ScannerMutex *mutex)
{
    (void)pthread_cond_wait(condition, mutex);
}
static inline void scanner_condition_signal(ScannerCondition *condition) { (void)pthread_cond_signal(condition); }
static inline void scanner_condition_broadcast(ScannerCondition *condition) { (void)pthread_cond_broadcast(condition); }
#endif

#endif