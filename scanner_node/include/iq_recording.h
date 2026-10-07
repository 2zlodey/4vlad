#ifndef SCANNER_NODE_IQ_RECORDING_H
#define SCANNER_NODE_IQ_RECORDING_H

#include "radio_frontend.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int scanner_iq_recording_open(const char *path, FILE **file, char *error, size_t error_size);
int scanner_iq_recording_write(FILE *file, const ScannerRadioFrontend *frontend, uint8_t channel,
                               uint16_t complex_samples, uint8_t sample_format, const uint8_t *iq, size_t iq_size,
                               char *error, size_t error_size);
void scanner_iq_recording_close(FILE **file);

#endif
