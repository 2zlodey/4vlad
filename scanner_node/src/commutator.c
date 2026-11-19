#include "commutator.h"
#include "scanner_log.h"

ScannerCommutatorResult scanner_commutator_select_antenna(uint8_t channel, uint8_t antenna, uint8_t *applied_antenna)
{
    LG('*', "Select logical antenna channel=%u antenna=%u; physical commutator control is deferred",
       (unsigned int)channel, (unsigned int)antenna);
    if (applied_antenna != NULL)
        *applied_antenna = antenna;
    return SCANNER_COMMUTATOR_OK;
}

ScannerCommutatorResult scanner_commutator_set_path(uint8_t channel, uint8_t value, int16_t *power_cdb)
{
    LG('*', "Set gain/attenuator path channel=%u value=%u; external commutator transport not implemented",
       (unsigned int)channel, (unsigned int)value);
    if (power_cdb != NULL)
        *power_cdb = 0;
    return SCANNER_COMMUTATOR_UNSUPPORTED;
}