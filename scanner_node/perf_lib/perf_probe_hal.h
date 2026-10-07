#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Initialize HAL resources used by perf module.
     * Contract: safe to call once during system startup before perf_init().
     * Expected result: subsequent HAL calls are ready and non-blocking.
     */
    void perf_hal_init(void);

    /*
     * Return current monotonic wall time in microseconds.
     * Contract: callable from normal task context; should not block.
     * Expected result: non-decreasing value across calls while running.
     */
    uint64_t perf_hal_now_us(void);

    /*
     * Return current free-running hardware counter value.
     * Contract: must be fast and safe in hot paths (scope begin/end).
     * Expected result: monotonically increasing modulo wraparound.
     */
    uint64_t perf_hal_counter(void);

    /*
     * Enter critical section and return opaque previous interrupt/state token.
     * Contract: every successful enter must be paired with perf_hal_critical_exit(state).
     * Expected result: token is valid only for the matching exit call.
     */
    uint32_t perf_hal_critical_enter(void);

    /*
     * Exit critical section using state from perf_hal_critical_enter().
     * Contract: pass exactly the token returned by the corresponding enter call.
     * Expected result: previous interrupt/state is restored.
     */
    void perf_hal_critical_exit(uint32_t state);

    /*
     * Emit formatted log output used by perf reports.
     * Contract: printf-compatible formatting; should tolerate short, frequent messages.
     * Expected result: best-effort output without altering perf state.
     */
    void perf_hal_printf(const char *fmt, ...);
    void perf_hal_set_log_sink(void (*sink)(const char *line));

#ifdef __cplusplus
}
#endif