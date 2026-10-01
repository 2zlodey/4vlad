// clang-format off
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif
// clang-format on

#include "device_config.h"
#include "protocol.h"
#include "udp_socket.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} Options;

static void print_usage(const char *program)
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

static int parse_options(int argc, char **argv, Options *options)
{
    int index;
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
            print_usage(argv[0]);
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
        {
            options->server_address = value;
        }
        else if (strcmp(argument, "--bind-address") == 0)
        {
            options->bind_address = value;
        }
        else if (strcmp(argument, "--device-json") == 0)
        {
            options->device_json = value;
        }
        else if (strcmp(argument, "--save-device-json") == 0)
        {
            options->save_device_json = value;
        }
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
            {
                options->server_port = (uint16_t)parsed;
            }
            else
            {
                options->local_port = (uint16_t)parsed;
            }
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
            {
                options->attempts = (unsigned int)parsed;
            }
            else if (strcmp(argument, "--handshake-timeout") == 0)
            {
                options->handshake_timeout_ms = (unsigned int)parsed;
            }
            else if (strcmp(argument, "--ver-timeout") == 0)
            {
                options->ver_timeout_ms = (unsigned int)parsed;
            }
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

static uint64_t monotonic_milliseconds(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return (uint64_t)time(NULL) * 1000u;
    }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static void wall_clock_parts(int64_t *seconds, int64_t *microseconds)
{
#ifdef _WIN32
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    ticks.QuadPart -= 116444736000000000ULL;
    *seconds = (int64_t)(ticks.QuadPart / 10000000ULL);
    *microseconds = (int64_t)((ticks.QuadPart % 10000000ULL) / 10ULL);
#else
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
    {
        *seconds = (int64_t)time(NULL);
        *microseconds = 0;
        return;
    }
    *seconds = (int64_t)now.tv_sec;
    *microseconds = (int64_t)(now.tv_nsec / 1000);
#endif
}

static int receive_expected(ScannerUdpSocket *socket_handle, const Options *options, unsigned int timeout_ms,
                            size_t expected_size, int allow_size_mismatch, ScannerDatagram *accepted, char *error,
                            size_t error_size)
{
    uint64_t deadline = monotonic_milliseconds() + timeout_ms;
    while (monotonic_milliseconds() < deadline)
    {
        uint64_t now = monotonic_milliseconds();
        unsigned int remaining = (unsigned int)(deadline - now);
        ScannerDatagram datagram;
        int result = scanner_udp_receive(socket_handle, remaining, &datagram, error, error_size);
        if (result < 0)
            return -1;

        if (result == 0)
            return 0;

        if (!scanner_same_endpoint(&datagram.source, options->server_address, options->server_port))
        {
            char source[64];
            scanner_endpoint_string(&datagram.source, source, sizeof(source));
            fprintf(stderr, "Ignoring datagram from unexpected endpoint %s\n", source);
            continue;
        }
        if (datagram.size != expected_size)
        {
            if (!allow_size_mismatch)
            {
                fprintf(stderr, "Ignoring %lu-byte datagram; expected %lu bytes\n", (unsigned long)datagram.size,
                        (unsigned long)expected_size);
                continue;
            }
            fprintf(stderr, "WARNING: accepting nonstandard %lu-byte session reply (expected %lu)\n",
                    (unsigned long)datagram.size, (unsigned long)expected_size);
        }
        *accepted = datagram;
        return 1;
    }
    return 0;
}

static int handle_radio_command(ScannerUdpSocket *socket_handle, const Options *options,
                                ScannerRadioInventory *inventory, const ScannerDatagram *datagram, char *error,
                                size_t error_size)
{
    uint8_t response[SCANNER_RADIO_FRONTENDS_RESPONSE_MAX_SIZE];
    size_t response_size;
    ScannerRadioFrontendsRequest frontends_request;
    ScannerSetActiveRadioRequest select_request;
    ScannerExitRequest exit_request;

    if (scanner_decode_radio_frontends_request(datagram->payload, datagram->size, &frontends_request))
    {
        response_size = scanner_encode_radio_frontends_response(response, sizeof(response),
                                                                frontends_request.request_id, inventory);
        if (response_size == 0)
        {
            snprintf(error, error_size, "Could not encode radio frontend capabilities");
            return 0;
        }
        if (!scanner_udp_send(socket_handle, options->server_address, options->server_port, response, response_size,
                              error, error_size))
            return 0;
        printf("Radio frontend query %" PRIu32 " answered: %lu frontend(s), active=%u\n", frontends_request.request_id,
               (unsigned long)inventory->count, (unsigned int)inventory->active_id);
        return 1;
    }

    if (scanner_decode_set_active_radio_request(datagram->payload, datagram->size, &select_request))
    {
        uint8_t status = scanner_radio_select(inventory, select_request.frontend_id) ? SCANNER_RADIO_STATUS_OK
                                                                                     : SCANNER_RADIO_STATUS_NOT_FOUND;
        response_size = scanner_encode_set_active_radio_response(response, select_request.request_id, status,
                                                                 inventory->active_id);
        if (response_size == 0
            || !scanner_udp_send(socket_handle, options->server_address, options->server_port, response, response_size,
                                 error, error_size))
            return 0;
        printf("Active radio selection %" PRIu32 ": frontend=%u status=%u\n", select_request.request_id,
               (unsigned int)select_request.frontend_id, (unsigned int)status);
        return 1;
    }

    if (scanner_decode_exit_request(datagram->payload, datagram->size, &exit_request))
    {
        response_size = scanner_encode_exit_response(response, exit_request.request_id, SCANNER_RADIO_STATUS_OK);
        if (response_size == 0
            || !scanner_udp_send(socket_handle, options->server_address, options->server_port, response, response_size,
                                 error, error_size))
            return 0;
        printf("Exit command %" PRIu32 " acknowledged; shutting down\n", exit_request.request_id);
        return 2;
    }

    fprintf(stderr, "Ignoring malformed or unsupported radio command\n");
    return 1;
}

static int wait_for_radio_commands(ScannerUdpSocket *socket_handle, const Options *options,
                                   ScannerRadioInventory *inventory, char *error, size_t error_size)
{
    for (;;)
    {
        ScannerDatagram datagram;
        int result = scanner_udp_receive(socket_handle, 1000, &datagram, error, error_size);
        if (result < 0)
            return 0;
        if (result == 0)
            continue;
        if (!scanner_same_endpoint(&datagram.source, options->server_address, options->server_port))
        {
            char source[64];
            scanner_endpoint_string(&datagram.source, source, sizeof(source));
            fprintf(stderr, "Ignoring radio command from unexpected endpoint %s\n", source);
            continue;
        }
        result = handle_radio_command(socket_handle, options, inventory, &datagram, error, error_size);
        if (result == 0)
            return 0;
        if (result == 2)
            return 1;
    }
}

static int run_node(const Options *options)
{
    ScannerDevice device;
    ScannerRadioInventory radio_inventory;
    ScannerUdpSocket socket_handle;
    ScannerHandshakeHeader header;
    uint8_t handshake[SCANNER_HANDSHAKE_SIZE];
    ScannerDatagram datagram;
    char error[256];
    unsigned int attempt;
    int result = 0;

    memset(&socket_handle, 0, sizeof(socket_handle));
    socket_handle.handle = SCANNER_INVALID_SOCKET;
    if (!device_load_json(options->device_json, &device, error, sizeof(error)))
    {
        fprintf(stderr, "Device JSON: %s\n", error);
        return 0;
    }
    if (options->save_device_json != NULL
        && !device_save_json(options->save_device_json, &device, error, sizeof(error)))
    {
        fprintf(stderr, "Device JSON save: %s\n", error);
        return 0;
    }

    scanner_radio_discover(&radio_inventory);
    printf("Radio frontends detected: %lu\n", (unsigned long)radio_inventory.count);
    for (attempt = 0; attempt < radio_inventory.count; ++attempt)
    {
        const ScannerRadioFrontend *frontend = &radio_inventory.frontends[attempt];
        printf("  frontend=%u backend=%s name=%s RX=%u TX=%u range=%" PRIu64 "..%" PRIu64
               "Hz/step=%u sample-rate=%u..%uHz/step=%u bandwidth=%u..%uHz/step=%u gain=%d..%d/step=%d"
               " centi-dB IQ=%u-bit/fmt%u AGC=0x%02x bw-options=%u\n",
               (unsigned int)frontend->id, scanner_radio_backend_name(frontend->backend), frontend->name,
               (unsigned int)frontend->rx_channels, (unsigned int)frontend->tx_channels, frontend->frequency_min_hz,
               frontend->frequency_max_hz, (unsigned int)frontend->frequency_step_hz,
               (unsigned int)frontend->sample_rate_min_hz, (unsigned int)frontend->sample_rate_max_hz,
               (unsigned int)frontend->sample_rate_step_hz, (unsigned int)frontend->bandwidth_min_hz,
               (unsigned int)frontend->bandwidth_max_hz, (unsigned int)frontend->bandwidth_step_hz,
               (int)frontend->gain_min_cdb, (int)frontend->gain_max_cdb, (int)frontend->gain_step_cdb,
               (unsigned int)frontend->sample_resolution_bits, (unsigned int)frontend->iq_sample_format,
               (unsigned int)frontend->agc_modes, (unsigned int)frontend->bandwidth_option_count);
    }

    if (!scanner_udp_open(&socket_handle, error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    if (!scanner_udp_bind(&socket_handle, options->bind_address, options->local_port, error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }

    header.counter = 1;
    wall_clock_parts(&header.seconds, &header.microseconds);
    header.reply_port = options->local_port;
    scanner_encode_handshake(handshake, &header, &device);

    printf("Scanner node %" PRId32 " -> %s:%u (local %s:%u)\n", device.id, options->server_address,
           (unsigned int)options->server_port, options->bind_address, (unsigned int)options->local_port);

    for (attempt = 1; attempt <= options->attempts; ++attempt)
    {
        if (!scanner_udp_send(&socket_handle, options->server_address, options->server_port, handshake,
                              sizeof(handshake), error, sizeof(error)))
        {
            fprintf(stderr, "%s\n", error);
            goto cleanup;
        }
        printf("Handshake attempt %u/%u sent (%u bytes)\n", attempt, options->attempts,
               (unsigned int)sizeof(handshake));
        result = receive_expected(&socket_handle, options, options->handshake_timeout_ms, SCANNER_SESSION_REPLY_SIZE,
                                  options->ignore_session_reply_size, &datagram, error, sizeof(error));
        if (result < 0)
        {
            fprintf(stderr, "%s\n", error);
            goto cleanup;
        }
        if (result == 1)
            break;

        fprintf(stderr, "No valid session reply before timeout\n");
    }
    if (attempt > options->attempts)
    {
        fprintf(stderr, "Handshake failed: no valid 52-byte session reply\n");
        goto cleanup;
    }

    printf("Session reply received; waiting for VER request\n");
    {
        uint64_t deadline = monotonic_milliseconds() + options->ver_timeout_ms;
        while (monotonic_milliseconds() < deadline)
        {
            uint64_t now = monotonic_milliseconds();
            unsigned int remaining = (unsigned int)(deadline - now);
            ScannerVerRequest request;
            uint8_t response[SCANNER_VER_RESPONSE_SIZE];
            result = scanner_udp_receive(&socket_handle, remaining, &datagram, error, sizeof(error));
            if (result < 0)
            {
                fprintf(stderr, "%s\n", error);
                goto cleanup;
            }
            if (result == 0)
                break;

            if (!scanner_same_endpoint(&datagram.source, options->server_address, options->server_port))
            {
                char source[64];
                scanner_endpoint_string(&datagram.source, source, sizeof(source));
                fprintf(stderr, "Ignoring VER candidate from %s\n", source);
                continue;
            }
            if (!scanner_decode_ver_request(datagram.payload, datagram.size, &request))
            {
                fprintf(stderr, "Ignoring malformed or unsupported command datagram\n");
                continue;
            }
            if (!scanner_encode_ver_response(response, request.request_id, options->software_version))
            {
                fprintf(stderr, "Invalid VER software version\n");
                goto cleanup;
            }
            if (!scanner_udp_send(&socket_handle, options->server_address, options->server_port, response,
                                  sizeof(response), error, sizeof(error)))
            {
                fprintf(stderr, "%s\n", error);
                goto cleanup;
            }
            printf("VER %" PRIu32 " answered with version %s (%u bytes)\n", request.request_id,
                   options->software_version, (unsigned int)sizeof(response));
            result = 1;
            break;
        }
    }
    if (result != 1)
    {
        fprintf(stderr, "Timed out waiting for a valid VER request\n");
        goto cleanup;
    }
    printf("Waiting for radio commands; Exit (0x06) closes the session\n");
    if (!wait_for_radio_commands(&socket_handle, options, &radio_inventory, error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        result = 0;
    }
    else
    {
        printf("Exit command completed\n");
    }

cleanup:
    scanner_udp_close(&socket_handle);
    return result == 1;
}

int main(int argc, char **argv)
{
    Options options;
    int parsed = parse_options(argc, argv, &options);
    if (parsed == 2)
        return 0;

    if (!parsed)
    {
        print_usage(argv[0]);
        return 2;
    }
    return run_node(&options) ? 0 : 1;
}