#ifndef SCANNER_NODE_COMMUTATOR_H
#define SCANNER_NODE_COMMUTATOR_H

#include <stdint.h>

typedef enum
{
    SCANNER_COMMUTATOR_UNSUPPORTED = 0,
    SCANNER_COMMUTATOR_OK = 1,
    SCANNER_COMMUTATOR_ERROR = -1
} ScannerCommutatorResult;

ScannerCommutatorResult scanner_commutator_select_antenna(uint8_t channel, uint8_t antenna, uint8_t *applied_antenna);
ScannerCommutatorResult scanner_commutator_set_path(uint8_t channel, uint8_t value, int16_t *power_cdb);

#endif