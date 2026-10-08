#include "commutator.h"
#include "scanner_log.h"

ScannerCommutatorResult scanner_commutator_select_antenna(uint8_t channel, uint8_t antenna, uint8_t *applied_antenna)
{
    LG('*', "Select antenna channel=%u antenna=%u; external commutator transport not implemented",
       (unsigned int)channel, (unsigned int)antenna);
    if (applied_antenna != NULL)
        *applied_antenna = 0;
    return SCANNER_COMMUTATOR_UNSUPPORTED;
}

ScannerCommutatorResult scanner_commutator_set_path(uint8_t channel, uint8_t value, int16_t *power_cdb)
{
    LG('*', "Set gain/attenuator path channel=%u value=%u; external commutator transport not implemented",
       (unsigned int)channel, (unsigned int)value);
    if (power_cdb != NULL)
        *power_cdb = 0;
    return SCANNER_COMMUTATOR_UNSUPPORTED;
}