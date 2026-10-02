#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef CONFIG_PERF_ENABLE
/*
 * Master compile-time switch.
 * 0: all PERF_* macros compile to no-ops.
 * 1: profiling code and runtime APIs are enabled.
 */
#define CONFIG_PERF_ENABLE 0
#endif

#ifndef CONFIG_PERF_STRINGS
/*
 * Include probe names in reports.
 * Disable to reduce ROM/RAM usage on constrained targets.
 */
#define CONFIG_PERF_STRINGS 1
#endif

#ifndef CONFIG_PERF_LOG_ENABLE
/*
 * Default runtime state for periodic reporting/log output.
 * Can still be toggled at runtime via perf_set_logging().
 */
#define CONFIG_PERF_LOG_ENABLE 1
#endif

#ifndef CONFIG_PERF_PROBES_MAX
/* Maximum number of unique probe IDs tracked at once. */
#define CONFIG_PERF_PROBES_MAX 32
#endif

#ifndef CONFIG_PERF_NAME_LEN
/* Maximum stored probe name length (including terminator handling internally). */
#define CONFIG_PERF_NAME_LEN 48
#endif

#ifndef CONFIG_PERF_REPORT_MS
/* Default interval used by perf_poll_report() for periodic printouts. */
#define CONFIG_PERF_REPORT_MS 1000u
#endif

    typedef struct
    {
        uint32_t count;      // Number of completed probe samples.
        uint32_t fail_count; // Number of samples marked as failed.

        /* Timing stats in microseconds. */
        uint64_t min_us;
        uint64_t max_us;
        uint64_t sum_us;

        /* Optional hardware counter stats (platform-specific units). */
        uint64_t min_cnt;
        uint64_t max_cnt;
        uint64_t sum_cnt;
    } perf_stats_t;

    typedef struct
    {
        int slot;           // Probe slot assigned by perf_scope_begin().
        uint64_t start_us;  // Start timestamp in microseconds.
        uint64_t start_cnt; // Start hardware counter value.
        uint8_t active;     // Internal guard: scope is active and should be closed.
        uint8_t failed;     // Internal marker used to increment fail_count at scope end.
    } perf_scope_t;

    void perf_init(void); // Init perf module once during startup before using probe scopes.
    
    void perf_set_enabled(bool enabled); // Globally enable/disable data collection at runtime.
    bool perf_is_enabled(void);          // Returns current runtime data-collection state.
    void perf_set_logging(bool enabled); // Enable/disable reporting output at runtime.
    bool perf_is_logging_enabled(void);  // Returns current reporting/logging state.
    
    void perf_reset_all(void);        // Clear all accumulated statistics
    void perf_report(void);           // Print current statistics snapshot.
    void perf_report_and_reset(void); // Print statistics and immediately clear them.
    void perf_poll_report(void);      // Call periodically to auto-report per CONFIG_PERF_REPORT_MS.
    
    /* Begin a measured scope manually; must be paired with perf_scope_end(). */
    void perf_scope_begin(perf_scope_t *scope, const char *name, uint32_t id);
    /* End a manually started scope and commit sample stats. */
    void perf_scope_end(perf_scope_t *scope);

    /* Mark the current scope sample as failed before ending it. */
    void perf_scope_fail(perf_scope_t *scope);
    /* Mark the active auto scope (PERF_SCOPE/PERF_SCOPE_ID) as failed. */
    void perf_fail_current(void);

    // And public macros
    // PERF_SCOPE("my_scope_name") for simple named scopes, or PERF_SCOPE_ID(123) for stable
    // numeric IDs (e.g. for loops or repeated probes).

    // PERF_FAIL_CURRENT() to mark the current active scope as failed (e.g. on error conditions)
    // before it ends and updates stats.

#if CONFIG_PERF_ENABLE && defined(__GNUC__)

    /* GCC cleanup handler used by PERF_SCOPE* for RAII-style scope closing. */
    void perf_scope_cleanup(perf_scope_t *scope);

/* Internal token pasting helpers used by PERF_SCOPE* macros. */
#define PERF_JOIN_INNER(a, b) a##b
#define PERF_JOIN(a, b) PERF_JOIN_INNER(a, b)

#if CONFIG_PERF_STRINGS

/*
 * Measure a lexical C scope with automatic begin/end.
 * Use at the top of a block/function: PERF_SCOPE("rx_loop");
 */
#define PERF_SCOPE(NAME)                                                                           \
    perf_scope_t PERF_JOIN(_perf_scope_, __LINE__) __attribute__((cleanup(perf_scope_cleanup)));   \
    perf_scope_begin(&PERF_JOIN(_perf_scope_, __LINE__), (NAME), __LINE__)

#else // !CONFIG_PERF_STRINGS

/* Same as PERF_SCOPE(NAME), but omits name storage when CONFIG_PERF_STRINGS=0. */
#define PERF_SCOPE(NAME)                                                                           \
    perf_scope_t PERF_JOIN(_perf_scope_, __LINE__) __attribute__((cleanup(perf_scope_cleanup)));   \
    perf_scope_begin(&PERF_JOIN(_perf_scope_, __LINE__), NULL, __LINE__)

#endif // CONFIG_PERF_STRINGS

/*
 * Measure a lexical scope with a stable custom numeric ID.
 * Prefer this for repeated probes where line numbers may change over time.
 */
#define PERF_SCOPE_ID(ID)                                                                          \
    perf_scope_t PERF_JOIN(_perf_scope_, __LINE__) __attribute__((cleanup(perf_scope_cleanup)));   \
    perf_scope_begin(&PERF_JOIN(_perf_scope_, __LINE__), NULL, (ID))

/* Mark the currently active PERF_SCOPE/PERF_SCOPE_ID sample as failed. */
#define PERF_FAIL_CURRENT() perf_fail_current()

#else // CONFIG_PERF_ENABLE && defined(__GNUC__)

#define PERF_SCOPE(NAME)                                                                           \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#define PERF_SCOPE_ID(ID)                                                                          \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#define PERF_FAIL_CURRENT()                                                                        \
    do                                                                                             \
    {                                                                                              \
    } while (0)

#endif // CONFIG_PERF_ENABLE && defined(__GNUC__)

#ifdef __cplusplus
}
#endif
