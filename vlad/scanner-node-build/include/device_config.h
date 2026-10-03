#ifndef SCANNER_NODE_DEVICE_CONFIG_H
#define SCANNER_NODE_DEVICE_CONFIG_H

#include <stdint.h>

#define SCANNER_NODE_ANTENNA_COUNT 16

typedef struct
{
    int32_t input;
    int32_t from;
    int32_t to;
    double polar;
    int32_t type;
    int32_t dbi;
    double direction;
} ScannerAntenna;

typedef struct
{
    int32_t id;
    double version;
    double longitude;
    double latitude;
    ScannerAntenna antennas[SCANNER_NODE_ANTENNA_COUNT];
} ScannerDevice;

int device_load_json(const char *filename, ScannerDevice *device, char *error,
                     unsigned long error_size);
int device_save_json(const char *filename, const ScannerDevice *device, char *error,
                     unsigned long error_size);

#endif