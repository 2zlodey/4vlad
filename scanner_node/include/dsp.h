#ifndef SCANNER_NODE_DSP_H
#define SCANNER_NODE_DSP_H

#include <stddef.h>
#include <stdint.h>

#define SCANNER_DSP_IQ_FORMAT_S8 1u
#define SCANNER_DSP_IQ_FORMAT_S16_Q11 2u

/*
 * Estimate the average complex-sample power of one captured I/Q window.
 * The result is a normalized dBFS level in signed centi-dB, not calibrated dBm.
 * This estimate does not separate noise from a coherent signal; it is the
 * current noise-floor proxy used by MEASURE and each SWEEP point.
 */
int scanner_dsp_estimate_noise_floor_dbfs(const uint8_t *iq, size_t iq_size, size_t complex_sample_count,
                                          uint8_t sample_format, int16_t *noise_floor_cdbfs);

#endif
