#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "vlad-dev.h"


// ============================================================
// Сохранение Device в JSON
// ============================================================

int device_save_json(const char *filename, const Device *dev){
    if (dev == NULL) return 0;
    // Корневой JSON-объект
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return 0;
    cJSON_AddNumberToObject(root, "id", dev->id);
    cJSON_AddNumberToObject(root, "version", dev->version);
    cJSON *coord = cJSON_CreateObject();
    if (coord == NULL)  {
        cJSON_Delete(root);
        return 0;
    }
    cJSON_AddNumberToObject(coord, "lon", dev->coord.lon);
    cJSON_AddNumberToObject(coord, "lat", dev->coord.lat);
    cJSON_AddItemToObject(root, "coord", coord);
    cJSON *rfin = cJSON_CreateArray();
    if (rfin == NULL) {
        cJSON_Delete(root);
        return 0;
    }
    cJSON_AddItemToObject(root, "rfin", rfin);
    for (int i = 0; i < 16; i++) {
        cJSON *antenna = cJSON_CreateObject();
        if (antenna == NULL) {
            cJSON_Delete(root);
            return 0;
        }
        cJSON_AddNumberToObject(antenna,"in",dev->rfin[i].in);
        cJSON_AddNumberToObject(antenna,"from",dev->rfin[i].from);
        cJSON_AddNumberToObject(antenna,"to",dev->rfin[i].to);
        cJSON_AddNumberToObject(antenna,"polar",dev->rfin[i].polar);
        cJSON_AddNumberToObject(antenna,"type",dev->rfin[i].type);
        cJSON_AddNumberToObject(antenna,"dBi",dev->rfin[i].dBi);
        cJSON_AddNumberToObject(antenna,"direction",dev->rfin[i].direction);
        cJSON_AddItemToArray(rfin, antenna);
    } //End of FOR


    // --------------------------------------------------------
    // Преобразуем JSON в текст
    // --------------------------------------------------------
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        cJSON_Delete(root);
        return 0;
    }
    // --------------------------------------------------------
    // Записываем JSON-файл
    // --------------------------------------------------------
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        free(json_string);
        cJSON_Delete(root);
        return 0;
    }
    fprintf(file, "%s\n", json_string);
    fclose(file);
    // Освобождаем память
    free(json_string);
    cJSON_Delete(root);
    return 1;
}


// ============================================================
// Загрузка Device из JSON
// ============================================================

int device_load_json(const char *filename, Device *dev) {
    if (dev == NULL)return 0;
    // --------------------------------------------------------
    // Открываем файл
    // --------------------------------------------------------
    FILE *file = fopen(filename, "r");
    if (file == NULL) return 0;
    // --------------------------------------------------------
    // Определяем размер файла
    // --------------------------------------------------------
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        return 0;
    }
    // --------------------------------------------------------
    // Выделяем память
    // --------------------------------------------------------
    char *json_string = malloc(file_size + 1);
    if (json_string == NULL) {
        fclose(file);
        return 0;
    }
    // --------------------------------------------------------
    // Читаем файл
    // --------------------------------------------------------
    size_t read_size = fread(json_string,1,file_size,file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(json_string);
        return 0;
    }
    json_string[file_size] = '\0';

    // --------------------------------------------------------
    // Парсим JSON
    // --------------------------------------------------------
    cJSON *root = cJSON_Parse(json_string);
    free(json_string);
    if (root == NULL) return 0;
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *version = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsNumber(id) || !cJSON_IsNumber(version)) {
        cJSON_Delete(root);
        return 0;
    }
    dev->id = id->valueint;
    dev->version = version->valuedouble;
    // --------------------------------------------------------
    // Coordinates
    // --------------------------------------------------------
    cJSON *coord = cJSON_GetObjectItem(root, "coord");
    if (!cJSON_IsObject(coord))    {
        cJSON_Delete(root);
        return 0;
    }
    cJSON *lon = cJSON_GetObjectItem(coord, "lon");
    cJSON *lat = cJSON_GetObjectItem(coord, "lat");
    if (!cJSON_IsNumber(lon) ||  !cJSON_IsNumber(lat)) {
        cJSON_Delete(root);
        return 0;
    }
    dev->coord.lon = lon->valuedouble;
    dev->coord.lat = lat->valuedouble;

    // --------------------------------------------------------
    // Antennas
    // --------------------------------------------------------
    cJSON *rfin = cJSON_GetObjectItem(root, "rfin");
    if (!cJSON_IsArray(rfin)) {
        cJSON_Delete(root);
        return 0;
    }
    int antenna_count = cJSON_GetArraySize(rfin);
    if (antenna_count != 16)  {
        cJSON_Delete(root);
        return 0;
    }
    for (int i = 0; i < 16; i++) {
        cJSON *antenna = cJSON_GetArrayItem(rfin, i);
        if (!cJSON_IsObject(antenna)) {
            cJSON_Delete(root);
            return 0;
        }
        cJSON *item;
        item = cJSON_GetObjectItem(antenna, "in");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].in = item->valueint;
        item = cJSON_GetObjectItem(antenna, "from");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].from = item->valueint;
        item = cJSON_GetObjectItem(antenna, "to");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].to = item->valueint;
        item = cJSON_GetObjectItem(antenna, "polar");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].polar = item->valuedouble;
        item = cJSON_GetObjectItem(antenna, "type");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].type = item->valueint;
        item = cJSON_GetObjectItem(antenna, "dBi");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].dBi = item->valueint;
        item = cJSON_GetObjectItem(antenna, "direction");
        if (!cJSON_IsNumber(item)) {
          cJSON_Delete(root);
          return 0;
        }
        dev->rfin[i].direction = item->valuedouble;
    }
    cJSON_Delete(root);
    return 1;

}
