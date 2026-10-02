# perf_lib Project Description

## Overview

This project is a lightweight embedded performance probing library written in C.
It measures execution time for code scopes and aggregates runtime statistics per probe.

The design is split into:
- A platform-independent probe core
- A platform abstraction layer (HAL)
- A host HAL for Windows/Linux benchmark builds
- A target-specific Zynq HAL

The library is intended for bare-metal or low-level firmware contexts where low overhead and simple integration are important.

## Main Components

### 1) Core Public API

Defined in `perf_probe.h`, implemented in `perf_probe.c`.

Key capabilities:
- Enable/disable probe collection at runtime
- Enable/disable reporting logs at runtime
- Start/end scoped measurements
- Mark a scope as failed
- Report and reset statistics
- Periodic auto-report polling

Important public types:
- `perf_stats_t`: Aggregated metrics for one probe
  - `count`, `fail_count`
  - `min_us`, `max_us`, `sum_us`
  - `min_cnt`, `max_cnt`, `sum_cnt`
- `perf_scope_t`: Active measurement scope state
  - slot index, start timestamps/counters, active/failed flags

### 2) HAL Interface

Defined in `perf_probe_hal.h`.

The core depends on these platform hooks:
- Initialization (`perf_hal_init`)
- Current time in microseconds (`perf_hal_now_us`)
- Critical section enter/exit (`perf_hal_critical_enter`, `perf_hal_critical_exit`)
- Logging output (`perf_hal_printf`)

### 3) Host HAL

Implemented in `perf_probe_hal_host.c` and selected by the scanner's desktop/host build.

Highlights:
- Uses `QueryPerformanceCounter` on Windows
- Uses `CLOCK_MONOTONIC` on Linux/Pi
- Uses a critical section / pthread mutex to protect shared probe statistics
- Uses `vprintf` for reports

### 4) Zynq HAL

Implemented in `perf_probe_hal_zynq.c`.

Highlights:
- Uses `no_os_get_time()` for microsecond wall-clock time
- Reads FPGA clock counter register via `Xil_In32`
- Uses `Xil_ExceptionDisable/Enable` for critical sections (simple model)
- Uses `xil_printf` for output
- Expects/derives board-specific counter address and frequency via macros:
  - `PERF_FPGA_CLK_CNT_ADDR`
  - `PERF_FPGA_COUNTER_HZ`

## How Measurement Works

1. `perf_scope_begin` finds or allocates a probe slot by name (if enabled) or ID.
2. It captures start microseconds and start counter values.
3. `perf_scope_end` captures end values and computes elapsed deltas.
4. Stats are updated:
   - count increment
   - min/max/sum in microseconds and counter ticks
   - fail count increment if scope was marked failed
5. Reporting prints per-probe aggregates.

The core keeps a small scope stack so `perf_fail_current()` can mark the currently active scope as failed.

## Configuration Macros

Defaults are in `perf_probe.h` and can be overridden at compile time.

- `CONFIG_PERF_ENABLE` (default 0): compile-time on/off switch
- `CONFIG_PERF_STRINGS` (default 1): store probe names
- `CONFIG_PERF_LOG_ENABLE` (default 1): include logging/reporting code
- `CONFIG_PERF_PROBES_MAX` (default 32): max distinct probe entries
- `CONFIG_PERF_NAME_LEN` (default 48): max stored name length
- `CONFIG_PERF_REPORT_MS` (default 1000): periodic poll report interval
- `CONFIG_PERF_SCOPE_STACK_MAX` (default 8, in `perf_probe.c`): nested scope tracking depth

When `CONFIG_PERF_ENABLE=0`, the implementation compiles to no-op stubs.

## Scope Macros and GCC Cleanup Support

In `perf_probe.h`, if GCC and performance are enabled:
- `PERF_SCOPE(name)` starts an auto-cleaned scope (RAII-like behavior)
- `PERF_SCOPE_ID(id)` starts a scope without a string name
- `PERF_FAIL_CURRENT()` marks the top active scope as failed

`PERF_SCOPE` relies on GCC `cleanup` attribute so scope end is called automatically on block exit.

## Reporting Behavior

`perf_report()` prints one line per used probe with non-zero data:
- sample count and fail count
- min/avg/max microseconds
- optional counter average and converted milliseconds (if counter frequency is known)

`perf_poll_report()` emits `perf_report_and_reset()` when the configured interval elapses.

## Thread/ISR Safety Notes

- Stats and scope stack updates are protected by HAL critical sections.
- The Zynq implementation notes that critical handling is simplified and may need stronger IRQ state save/restore in complex contexts.
- If used from multiple contexts (threads/ISRs), verify the HAL critical model is sufficient.

## File-by-File Summary

- `.clang-format`: Formatting policy (LLVM-based, custom brace wrapping, 100-column limit)
- `perf_probe.h`: Public API, config macros, scope helper macros
- `perf_probe.c`: Core probe registry, scope lifecycle, aggregation, reporting
- `perf_probe_hal.h`: Platform abstraction contract
- `perf_probe_hal_host.c`: Windows/Linux monotonic-clock host HAL
- `perf_probe_hal_zynq.c`: Zynq/FPGA-counter-backed HAL implementation

## Typical Integration Flow

1. Add `perf_probe.c` and one HAL file for your target to your build.
2. Ensure required platform headers and counter sources are available.
3. Call `perf_init()` during startup.
4. Instrument hot paths with `PERF_SCOPE("name")` or explicit begin/end calls.
5. Periodically call `perf_poll_report()` or manually call `perf_report()`.

## Practical Constraints

- Probe table is fixed-size (`CONFIG_PERF_PROBES_MAX`), so new probes fail allocation once full.
- Name-based matching is exact and bounded by configured name length.
- Scope timestamps and aggregate timing statistics are 64-bit; the Zynq hardware counter source itself is 32-bit and its wrap behavior depends on the configured counter.
- Logging cost depends on target output backend (`printf` or `xil_printf`).

## Manual Test Build and Run

Manual host-side test instructions for Windows are documented in `TESTING.md`.
