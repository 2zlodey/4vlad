#ifndef DEVICE_H
#define DEVICE_H

#include <stdio.h>

#pragma pack(push, 1)
typedef struct
{
    double lon;
    double lat;
} Coordinates;

typedef struct
{
    int in;
    int from;
    int to;
    double polar;
    int type;
    int dBi;
    double direction;
} Antenna;
typedef struct
{
    int id;
    double version;
    Coordinates coord;
    Antenna rfin[16];
} Device;
#pragma pack(pop)

int device_save_json(const char *filename, const Device *dev);
int device_load_json(const char *filename, Device *dev);

#endif
