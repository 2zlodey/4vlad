#ifndef SCANNER_NODE_RADIO_CAPTURE_BACKEND_H
#define SCANNER_NODE_RADIO_CAPTURE_BACKEND_H

#include "radio_frontend.h"

int scanner_radio_capture_stub(ScannerRadioFrontend *frontend, uint16_t complex_pairs, uint8_t *output,
                               size_t *output_size, uint8_t *sample_format);
int scanner_radio_capture_hackrf(ScannerRadioFrontend *frontend, uint8_t channel, uint16_t complex_pairs,
                                 uint8_t *output, size_t required_size, size_t *output_size, uint8_t *sample_format,
                                 unsigned int timeout_ms);
int scanner_radio_capture_bladerf(ScannerRadioFrontend *frontend, uint8_t channel, uint16_t complex_pairs,
                                  uint8_t *output, size_t required_size, size_t *output_size, uint8_t *sample_format,
                                  unsigned int timeout_ms);
int scanner_radio_prepare_bladerf_capture(ScannerRadioFrontend *frontend);

#endif