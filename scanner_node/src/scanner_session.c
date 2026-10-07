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

#include "scanner_session.h"

#include "protocol.h"
#include "scanner_log.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static uint64_t monotonic_milliseconds(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return (uint64_t)time(NULL) * 1000u;
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

static int receive_expected(ScannerUdpSocket *socket_handle, const ScannerOptions *options, unsigned int timeout_ms,
                            size_t expected_size, ScannerDatagram *accepted, char *error, size_t error_size)
{
    uint64_t deadline = monotonic_milliseconds() + timeout_ms;
    while (monotonic_milliseconds() < deadline)
    {
        uint64_t now = monotonic_milliseconds();
        unsigned int remaining = (unsigned int)(deadline - now);
        ScannerDatagram datagram;
        int result = scanner_udp_receive(socket_handle, remaining, &datagram, error, error_size);
        if (result <= 0)
        {
            LG(result < 0 ? '!' : '*', "Session reply receive %s", result < 0 ? "failed" : "timed out");
            return result;
        }

        if (!scanner_same_endpoint(&datagram.source, options->server_address, options->server_port))
        {
            char source[64];
            scanner_endpoint_string(&datagram.source, source, sizeof(source));
            LG('!', "Ignoring datagram from unexpected endpoint %s", source);
            continue;
        }
        if (datagram.size != expected_size)
        {
            if (!options->ignore_session_reply_size)
            {
                LG('!', "Ignoring %lu-byte datagram; expected %lu bytes", (unsigned long)datagram.size,
                   (unsigned long)expected_size);
                continue;
            }
            LG('i', "WARNING: accepting nonstandard %lu-byte session reply (expected %lu)",
               (unsigned long)datagram.size, (unsigned long)expected_size);
        }
        *accepted = datagram;
        LG('<', "Session reply accepted");
        return 1;
    }
    return 0;
}

static int exchange_handshake(ScannerUdpSocket *socket_handle, const ScannerOptions *options,
                              const ScannerDevice *device, char *error, size_t error_size)
{
    ScannerHandshakeHeader header;
    uint8_t handshake[SCANNER_HANDSHAKE_SIZE];
    ScannerDatagram reply;
    unsigned int attempt;

    header.counter = 1;
    wall_clock_parts(&header.seconds, &header.microseconds);
    header.reply_port = options->local_port;
    scanner_encode_handshake(handshake, &header, device);

    LG('i', "Scanner node %" PRId32 " -> %s:%u (local %s:%u)", device->id, options->server_address,
       (unsigned int)options->server_port, options->bind_address, (unsigned int)options->local_port);

    for (attempt = 1; attempt <= options->attempts; ++attempt)
    {
        int result;
        if (!scanner_udp_send(socket_handle, options->server_address, options->server_port, handshake,
                              sizeof(handshake), error, error_size))
        {
            LG('!', "Handshake attempt %u send failed", attempt);
            return 0;
        }
        LG('>', "Handshake attempt %u/%u sent (%u bytes)", attempt, options->attempts, (unsigned int)sizeof(handshake));
        result = receive_expected(socket_handle, options, options->handshake_timeout_ms, SCANNER_SESSION_REPLY_SIZE,
                                  &reply, error, error_size);
        if (result < 0)
        {
            LG('!', "Handshake attempt %u receive failed", attempt);
            return 0;
        }
        if (result == 1)
        {
            LG('+', "Handshake completed on attempt %u", attempt);
            return 1;
        }
        LG('*', "No valid session reply before timeout");
    }
    LG('!', "Handshake failed: no valid 52-byte session reply");
    return 0;
}

static int exchange_version(ScannerUdpSocket *socket_handle, const ScannerOptions *options, char *error,
                            size_t error_size)
{
    ScannerDatagram datagram;
    uint64_t deadline = monotonic_milliseconds() + options->ver_timeout_ms;
    LG('i', "Session reply received; waiting for VER request");
    while (monotonic_milliseconds() < deadline)
    {
        uint64_t now = monotonic_milliseconds();
        unsigned int remaining = (unsigned int)(deadline - now);
        ScannerVerRequest request;
        uint8_t response[SCANNER_VER_RESPONSE_SIZE];
        int result = scanner_udp_receive(socket_handle, remaining, &datagram, error, error_size);
        if (result < 0)
        {
            LG('!', "VER request receive failed");
            return 0;
        }
        if (result == 0)
            break;

        if (!scanner_same_endpoint(&datagram.source, options->server_address, options->server_port))
        {
            char source[64];
            scanner_endpoint_string(&datagram.source, source, sizeof(source));
            LG('!', "Ignoring VER candidate from %s", source);
            continue;
        }
        if (!scanner_decode_ver_request(datagram.payload, datagram.size, &request))
        {
            LG('!', "Ignoring malformed or unsupported VER command datagram");
            continue;
        }
        LG('<', "VER request %" PRIu32 " accepted", request.request_id);
        if (!scanner_encode_ver_response(response, request.request_id, options->software_version))
        {
            LG('!', "Invalid VER software version");
            return 0;
        }
        if (!scanner_udp_send(socket_handle, options->server_address, options->server_port, response, sizeof(response),
                              error, error_size))
        {
            LG('!', "VER %" PRIu32 " response send failed", request.request_id);
            return 0;
        }
        LG('>', "VER %" PRIu32 " answered with version %s (%u bytes)", request.request_id, options->software_version,
           (unsigned int)sizeof(response));
        return 1;
    }
    LG('!', "Timed out waiting for a valid VER request");
    return 0;
}

int scanner_session_establish(ScannerUdpSocket *socket_handle, const ScannerOptions *options,
                              const ScannerDevice *device, char *error, size_t error_size)
{
    if (error != NULL && error_size > 0)
        error[0] = '\0';
    if (socket_handle == NULL || options == NULL || device == NULL)
    {
        LG('!', "Session establishment failed: invalid arguments");
        return 0;
    }
    if (!exchange_handshake(socket_handle, options, device, error, error_size)
        || !exchange_version(socket_handle, options, error, error_size))
    {
        LG('!', "Session establishment failed");
        return 0;
    }
    LG('+', "Session established");
    return 1;
}
