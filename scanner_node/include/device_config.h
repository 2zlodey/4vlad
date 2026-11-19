#ifndef SCANNER_NODE_DEVICE_CONFIG_H
#define SCANNER_NODE_DEVICE_CONFIG_H

#include <stdint.h>

#define SCANNER_NODE_ANTENNA_COUNT 16
#define SCANNER_NODE_MAX_RX_CHANNELS 2

typedef enum
{
    SCANNER_ANTENNA_ROUTE_SWITCHED = 0,
    SCANNER_ANTENNA_ROUTE_FIXED = 1
} ScannerAntennaRouteMode;

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
    ScannerAntennaRouteMode mode;
    int32_t fixed_antenna_input;
} ScannerAntennaRoute;

typedef struct
{
    int32_t id;
    double version;
    double longitude;
    double latitude;
    ScannerAntenna antennas[SCANNER_NODE_ANTENNA_COUNT];
    ScannerAntennaRoute antenna_routes[SCANNER_NODE_MAX_RX_CHANNELS];
} ScannerDevice;

int device_load_json(const char *filename, ScannerDevice *device, char *error, unsigned long error_size);
int device_save_json(const char *filename, const ScannerDevice *device, char *error, unsigned long error_size);

#endif