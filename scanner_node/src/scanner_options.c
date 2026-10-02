#include "scanner_options.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scanner_options_print_usage(const char *program)
{
    printf(
        "Usage: %s [options]\n"
        "  --server IPv4         Server IPv4 address (default 82.165.20.164)\n"
        "  --server-port N       Server UDP port (default 2653)\n"
        "  --bind-address IPv4   Local bind address (default 0.0.0.0)\n"
        "  --local-port N        Local/reply UDP port (default 3333)\n"
        "  --device-json PATH    Input device JSON (default ../scanner-node-build/device.json)\n"
        "  --save-device-json P  Write loaded Device JSON to P, then continue\n"
        "  --software-version V  VER ASCII version, 1-10 bytes (default 1.0.0.0)\n"
        "  --attempts N          Handshake attempts (default 5)\n"
        "  --handshake-timeout N Handshake timeout in ms (default 2000)\n"
        "  --ver-timeout N       VER request timeout in ms (default 3000)\n"
        "  --ignore-session-reply-size Accept any-size datagram from the server\n"
        "                          as the session reply (diagnostics only)\n",
        program);
}

static int parse_unsigned(const char *text, unsigned long maximum, unsigned long *value)
{
    char *end = NULL;
    unsigned long parsed;
    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return 0;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 || parsed > maximum)
        return 0;

    *value = parsed;
    return 1;
}

int scanner_options_parse(int argc, char **argv, ScannerOptions *options)
{
    int index;
    if (options == NULL)
        return 0;
    memset(options, 0, sizeof(*options));
    options->server_address = "82.165.20.164";
    options->bind_address = "0.0.0.0";
    options->device_json = "../scanner-node-build/device.json";
    options->software_version = "1.0.0.0";
    options->server_port = 2653;
    options->local_port = 3333;
    options->attempts = 5;
    options->handshake_timeout_ms = 2000;
    options->ver_timeout_ms = 3000;

    for (index = 1; index < argc; ++index)
    {
        const char *argument = argv[index];
        const char *value;
        unsigned long parsed;
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0)
        {
            scanner_options_print_usage(argv[0]);
            return 2;
        }
        if (strcmp(argument, "--ignore-session-reply-size") == 0)
        {
            options->ignore_session_reply_size = 1;
            continue;
        }
        if (index + 1 >= argc)
        {
            fprintf(stderr, "Missing value for %s\n", argument);
            return 0;
        }
        value = argv[++index];
        if (strcmp(argument, "--server") == 0)
            options->server_address = value;
        else if (strcmp(argument, "--bind-address") == 0)
            options->bind_address = value;
        else if (strcmp(argument, "--device-json") == 0)
            options->device_json = value;
        else if (strcmp(argument, "--save-device-json") == 0)
            options->save_device_json = value;
        else if (strcmp(argument, "--software-version") == 0)
        {
            if (strlen(value) == 0 || strlen(value) > 10)
            {
                fprintf(stderr, "--software-version must be 1 to 10 bytes\n");
                return 0;
            }
            options->software_version = value;
        }
        else if (strcmp(argument, "--server-port") == 0 || strcmp(argument, "--local-port") == 0)
        {
            if (!parse_unsigned(value, 65535, &parsed))
            {
                fprintf(stderr, "Invalid port for %s\n", argument);
                return 0;
            }
            if (strcmp(argument, "--server-port") == 0)
                options->server_port = (uint16_t)parsed;
            else
                options->local_port = (uint16_t)parsed;
        }
        else if (strcmp(argument, "--attempts") == 0 || strcmp(argument, "--handshake-timeout") == 0
                 || strcmp(argument, "--ver-timeout") == 0)
        {
            unsigned long maximum = strcmp(argument, "--attempts") == 0 ? 100 : 60000;
            if (!parse_unsigned(value, maximum, &parsed))
            {
                fprintf(stderr, "Invalid positive integer for %s\n", argument);
                return 0;
            }
            if (strcmp(argument, "--attempts") == 0)
                options->attempts = (unsigned int)parsed;
            else if (strcmp(argument, "--handshake-timeout") == 0)
                options->handshake_timeout_ms = (unsigned int)parsed;
            else
                options->ver_timeout_ms = (unsigned int)parsed;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argument);
            return 0;
        }
    }
    return 1;
}
