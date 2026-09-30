#include "device_config.h"

#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, unsigned long error_size, const char *message)
{
    if (error != NULL && error_size > 0)
    {
        snprintf(error, (size_t)error_size, "%s", message);
    }
}

static const cJSON *required_item(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static int read_number(const cJSON *object, const char *name, double *value)
{
    const cJSON *item = required_item(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble))
    {
        return 0;
    }
    *value = item->valuedouble;
    return 1;
}

static int read_int32(const cJSON *object, const char *name, int32_t *value)
{
    double number;
    if (!read_number(object, name, &number) || trunc(number) != number || number < (double)INT32_MIN
        || number > (double)INT32_MAX)
    {
        return 0;
    }
    *value = (int32_t)number;
    return 1;
}

static int parse_antenna(const cJSON *item, ScannerAntenna *antenna)
{
    if (!cJSON_IsObject(item))
    {
        return 0;
    }
    return read_int32(item, "in", &antenna->input) && read_int32(item, "from", &antenna->from)
           && read_int32(item, "to", &antenna->to) && read_number(item, "polar", &antenna->polar)
           && read_int32(item, "type", &antenna->type) && read_int32(item, "dBi", &antenna->dbi)
           && read_number(item, "direction", &antenna->direction);
}

int device_load_json(const char *filename, ScannerDevice *device, char *error,
                     unsigned long error_size)
{
    FILE *file;
    long file_size;
    char *json_text;
    size_t bytes_read;
    cJSON *root;
    const cJSON *coord;
    const cJSON *rfin;
    ScannerDevice parsed;
    int index;

    if (filename == NULL || device == NULL)
    {
        set_error(error, error_size, "Invalid JSON load argument");
        return 0;
    }
    file = fopen(filename, "rb");
    if (file == NULL)
    {
        set_error(error, error_size, "Cannot open device JSON file");
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0
        || fseek(file, 0, SEEK_SET) != 0 || file_size > 1024 * 1024)
    {
        fclose(file);
        set_error(error, error_size, "Invalid or oversized device JSON file");
        return 0;
    }

    json_text = (char *)malloc((size_t)file_size + 1);
    if (json_text == NULL)
    {
        fclose(file);
        set_error(error, error_size, "Out of memory reading device JSON");
        return 0;
    }
    bytes_read = fread(json_text, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size)
    {
        free(json_text);
        set_error(error, error_size, "Failed to read complete device JSON file");
        return 0;
    }
    json_text[file_size] = '\0';

    root = cJSON_ParseWithLength(json_text, bytes_read);
    free(json_text);
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        set_error(error, error_size, "Device JSON root must be an object");
        return 0;
    }

    coord = required_item(root, "coord");
    rfin = required_item(root, "rfin");
    if (!cJSON_IsObject(coord) || !cJSON_IsArray(rfin)
        || cJSON_GetArraySize(rfin) != SCANNER_NODE_ANTENNA_COUNT
        || !read_int32(root, "id", &parsed.id) || !read_number(root, "version", &parsed.version)
        || !read_number(coord, "lon", &parsed.longitude)
        || !read_number(coord, "lat", &parsed.latitude))
    {
        cJSON_Delete(root);
        set_error(error, error_size,
                  "Device JSON requires id/version, coord lon/lat and exactly 16 antennas");
        return 0;
    }

    for (index = 0; index < SCANNER_NODE_ANTENNA_COUNT; ++index)
    {
        if (!parse_antenna(cJSON_GetArrayItem(rfin, index), &parsed.antennas[index]))
        {
            cJSON_Delete(root);
            set_error(error, error_size, "Invalid antenna JSON entry");
            return 0;
        }
    }
    cJSON_Delete(root);
    *device = parsed;
    return 1;
}

static int add_number(cJSON *object, const char *name, double value)
{
    return cJSON_AddNumberToObject(object, name, value) != NULL;
}

int device_save_json(const char *filename, const ScannerDevice *device, char *error,
                     unsigned long error_size)
{
    cJSON *root = NULL;
    cJSON *coord = NULL;
    cJSON *rfin = NULL;
    char *json_text = NULL;
    FILE *file = NULL;
    int index;
    int success = 0;

    if (filename == NULL || device == NULL)
    {
        set_error(error, error_size, "Invalid JSON save argument");
        return 0;
    }
    root = cJSON_CreateObject();
    coord = cJSON_CreateObject();
    rfin = cJSON_CreateArray();
    if (root == NULL || coord == NULL || rfin == NULL || !add_number(root, "id", device->id)
        || !add_number(root, "version", device->version)
        || !add_number(coord, "lon", device->longitude)
        || !add_number(coord, "lat", device->latitude))
    {
        set_error(error, error_size, "Could not allocate device JSON structure");
        goto cleanup;
    }
    cJSON_AddItemToObject(root, "coord", coord);
    coord = NULL;
    cJSON_AddItemToObject(root, "rfin", rfin);
    rfin = NULL;

    for (index = 0; index < SCANNER_NODE_ANTENNA_COUNT; ++index)
    {
        const ScannerAntenna *antenna = &device->antennas[index];
        cJSON *item = cJSON_CreateObject();
        if (item == NULL || !add_number(item, "in", antenna->input)
            || !add_number(item, "from", antenna->from) || !add_number(item, "to", antenna->to)
            || !add_number(item, "polar", antenna->polar)
            || !add_number(item, "type", antenna->type) || !add_number(item, "dBi", antenna->dbi)
            || !add_number(item, "direction", antenna->direction))
        {
            cJSON_Delete(item);
            set_error(error, error_size, "Could not encode antenna JSON entry");
            goto cleanup;
        }
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(root, "rfin"), item);
    }

    json_text = cJSON_Print(root);
    if (json_text == NULL)
    {
        set_error(error, error_size, "Could not serialize device JSON");
        goto cleanup;
    }
    file = fopen(filename, "wb");
    if (file == NULL)
    {
        set_error(error, error_size, "Cannot open output JSON file");
        goto cleanup;
    }
    if (fprintf(file, "%s\n", json_text) < 0)
    {
        set_error(error, error_size, "Failed to write device JSON file");
        goto cleanup;
    }
    if (fclose(file) != 0)
    {
        file = NULL;
        set_error(error, error_size, "Failed to close device JSON file");
        goto cleanup;
    }
    file = NULL;
    success = 1;

cleanup:
    if (file != NULL)
    {
        fclose(file);
    }
    free(json_text);
    cJSON_Delete(rfin);
    cJSON_Delete(coord);
    cJSON_Delete(root);
    return success;
}