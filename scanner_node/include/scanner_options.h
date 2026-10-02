#ifndef SCANNER_NODE_OPTIONS_H
#define SCANNER_NODE_OPTIONS_H

#include <stdint.h>

typedef struct
{
    const char *server_address;
    const char *bind_address;
    const char *device_json;
    const char *save_device_json;
    const char *software_version;
    uint16_t server_port;
    uint16_t local_port;
    unsigned int attempts;
    unsigned int handshake_timeout_ms;
    unsigned int ver_timeout_ms;
    int ignore_session_reply_size;
} ScannerOptions;

void scanner_options_print_usage(const char *program);
int scanner_options_parse(int argc, char **argv, ScannerOptions *options);

#endif
