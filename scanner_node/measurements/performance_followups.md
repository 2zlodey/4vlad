# BladeRF Sweep Performance Follow-ups

Real-hardware baseline: `sweep_perf_20261002.log`. Buffer-reuse/no-readback run: `sweep_perf_optimized_20261002.log`.

The BladeRF run improved from 13.360555 s (26.665 ms/point average) to 13.103593 s (26.152 ms/point average), about 1.9%. HackRF was effectively unchanged. The small gain is consistent with the fact that sync configuration and RX start/stop still happen per point; only the raw scratch allocation and BladeRF frequency readback were removed from the per-point path.

## Deferred Work

- Add per-stage timers for frequency tune, sync configuration, RX start/stop, sample transfer, IQ conversion, and DSP/classification. The aggregate point time is not enough to attribute bottlenecks.
- Replace per-hop synchronous stream teardown with a continuously enabled, timestamp-aware RX reader. A hardware smoke test showed libbladeRF reports `sync rx invalid: not initialized` if the RX module is disabled and the next capture skips `bladerf_sync_config`; simply hoisting that call is not valid. A continuous reader must identify and discard pre-retune/settling samples, handle backlog and overruns, and associate each IQ window with its tune frequency.
- Compare HOST and FPGA tuning modes on the installed Pi/libbladeRF/FPGA combination. Record library, firmware, FPGA versions and selected mode; do not assume desktop timing figures apply.
- Evaluate timestamped scheduled retunes and quick-tune profiles after the continuous-reader path exists. Verify FPGA/firmware support, queue limits, profile refresh and phase-noise impact.
- Revisit scan grid and effective bandwidth. Current 1.75 MHz bandwidth with 10 MHz spacing leaves large uncovered intervals. Test wider sample rates/bandwidths, overlap, flatness and classifier resolution before reducing hop count.
- If profiling shows DSP is material, reuse FFT/window/scratch storage or benchmark an optimized FFT library while checking classifier output equivalence.
- Tune sync buffer size/count and SC8 versus SC16 only with measured overrun rate, frequency settling and detection quality.
