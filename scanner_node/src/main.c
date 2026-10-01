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
#include "radio_worker.h"
#include "udp_socket.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCANNER_CAPTURE_TIMEOUT_MS 2000u

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

static int store_worker_response(ScannerRadioWorkerResult *worker_result, const uint8_t *response, size_t response_size)
{
    if (response_size == 0 || response_size > sizeof(worker_result->payload))
    {
        snprintf(worker_result->error, sizeof(worker_result->error), "%s", "Could not encode radio command response");
        return 0;
    }
    memcpy(worker_result->payload, response, response_size);
    worker_result->size = response_size;
    return 1;
}

static int handle_radio_command(ScannerRadioWorker *worker, ScannerRadioInventory *inventory,
                                const ScannerDatagram *datagram, ScannerRadioWorkerResult *worker_result, void *context)
{
    uint8_t response[SCANNER_RADIO_COMMAND_RESPONSE_MAX_SIZE];
    size_t response_size;
    ScannerRadioFrontendsRequest frontends_request;
    ScannerSetActiveRadioRequest select_request;
    ScannerExitRequest exit_request;
    ScannerSetFrequencyRequest set_frequency_request;
    ScannerGetFrequencyRequest get_frequency_request;
    ScannerSetU32Request set_u32_request;
    ScannerGetValueRequest get_value_request;
    ScannerSetGainRequest set_gain_request;
    ScannerMeasureFrequencyRequest measure_frequency_request;
    ScannerSweepRequest sweep_request;
    ScannerRawIqRequest raw_iq_request;
    const ScannerRadioFrontend *active_frontend;
    (void)context;

    if (scanner_decode_radio_frontends_request(datagram->payload, datagram->size, &frontends_request))
    {
        response_size = scanner_encode_radio_frontends_response(response, sizeof(response),
                                                                frontends_request.request_id, inventory);
        if (response_size == 0)
        {
            snprintf(worker_result->error, sizeof(worker_result->error), "%s",
                     "Could not encode radio frontend capabilities");
            worker_result->fatal = 1;
            return 0;
        }
        if (!store_worker_response(worker_result, response, response_size))
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
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        printf("Active radio selection %" PRIu32 ": frontend=%u status=%u\n", select_request.request_id,
               (unsigned int)select_request.frontend_id, (unsigned int)status);
        return 1;
    }

    if (scanner_decode_set_frequency_request(datagram->payload, datagram->size, &set_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t actual_frequency_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_frequency_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if ((uint64_t)set_frequency_request.frequency_khz * 1000u < active_frontend->frequency_min_hz
                 || (uint64_t)set_frequency_request.frequency_khz * 1000u > active_frontend->frequency_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!scanner_radio_set_frequency(inventory, set_frequency_request.channel,
                                              (uint64_t)set_frequency_request.frequency_khz * 1000u,
                                              &actual_frequency_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;

        response_size = scanner_encode_frequency_response(response, set_frequency_request.request_id,
                                                          SCANNER_SET_FREQUENCY_COMMAND, status,
                                                          set_frequency_request.channel, actual_frequency_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        printf("Set frequency request %" PRIu32 ": channel=%u requested=%u kHz status=%u actual=%" PRIu64 " Hz\n",
               set_frequency_request.request_id, (unsigned int)set_frequency_request.channel,
               (unsigned int)set_frequency_request.frequency_khz, (unsigned int)status, actual_frequency_hz);
        return 1;
    }

    if (scanner_decode_get_frequency_request(datagram->payload, datagram->size, &get_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t frequency_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_frequency_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else
        {
            int get_result = scanner_radio_get_frequency(inventory, get_frequency_request.channel, &frequency_hz);
            if (get_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (get_result < 0)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        }

        response_size = scanner_encode_frequency_response(response, get_frequency_request.request_id,
                                                          SCANNER_GET_FREQUENCY_COMMAND, status,
                                                          get_frequency_request.channel, frequency_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        printf("Get frequency request %" PRIu32 ": channel=%u status=%u frequency=%" PRIu64 " Hz\n",
               get_frequency_request.request_id, (unsigned int)get_frequency_request.channel, (unsigned int)status,
               frequency_hz);
        return 1;
    }

    if (scanner_decode_set_sample_rate_request(datagram->payload, datagram->size, &set_u32_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t actual_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_u32_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (set_u32_request.value < active_frontend->sample_rate_min_hz
                 || set_u32_request.value > active_frontend->sample_rate_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_SAMPLE_RATE))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_sample_rate(inventory, set_u32_request.channel, set_u32_request.value, &actual_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        response_size = scanner_encode_u32_setting_response(response, set_u32_request.request_id,
                                                            SCANNER_SET_SAMPLE_RATE_COMMAND, status,
                                                            set_u32_request.channel, actual_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        printf("Set sample rate id=%" PRIu32 " status=%u applied=%u Hz\n", set_u32_request.request_id,
               (unsigned int)status, (unsigned int)actual_hz);
        return 1;
    }

    if (scanner_decode_get_sample_rate_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t value_hz = 0;
        int get_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_SAMPLE_RATE))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else
        {
            get_result = scanner_radio_get_sample_rate(inventory, get_value_request.channel, &value_hz);
            if (get_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (get_result < 0)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        }
        response_size = scanner_encode_u32_setting_response(response, get_value_request.request_id,
                                                            SCANNER_GET_SAMPLE_RATE_COMMAND, status,
                                                            get_value_request.channel, value_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_set_bandwidth_request(datagram->payload, datagram->size, &set_u32_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint32_t actual_hz = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_u32_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (set_u32_request.value < active_frontend->bandwidth_min_hz
                 || set_u32_request.value > active_frontend->bandwidth_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_BANDWIDTH))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_bandwidth(inventory, set_u32_request.channel, set_u32_request.value, &actual_hz))
            status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
        response_size = scanner_encode_u32_setting_response(response, set_u32_request.request_id,
                                                            SCANNER_SET_BANDWIDTH_COMMAND, status,
                                                            set_u32_request.channel, actual_hz);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_set_gain_request(datagram->payload, datagram->size, SCANNER_SET_LNA_GAIN_COMMAND,
                                        &set_gain_request)
        || scanner_decode_set_gain_request(datagram->payload, datagram->size, SCANNER_SET_VGA_GAIN_COMMAND,
                                           &set_gain_request))
    {
        uint8_t command = datagram->payload[4];
        ScannerRadioGainStage stage = command == SCANNER_SET_LNA_GAIN_COMMAND ? SCANNER_RADIO_GAIN_LNA
                                                                              : SCANNER_RADIO_GAIN_VGA;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint8_t actual_db = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (set_gain_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_GAIN))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_gain_stage(inventory, set_gain_request.channel, stage, set_gain_request.gain_db,
                                               &actual_db))
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        response_size = scanner_encode_gain_stage_response(response, set_gain_request.request_id, command, status,
                                                           set_gain_request.channel, actual_db);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_get_gain_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        int16_t gain_cdb = 0;
        int gain_result;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_GAIN))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else
        {
            gain_result = scanner_radio_get_total_gain(inventory, get_value_request.channel, &gain_cdb);
            if (gain_result == 0)
                status = SCANNER_RADIO_STATUS_HARDWARE_ERROR;
            else if (gain_result < 0)
                status = SCANNER_RADIO_STATUS_NOT_CONFIGURED;
        }
        response_size = scanner_encode_gain_response(response, get_value_request.request_id, status,
                                                     get_value_request.channel, gain_cdb);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_measure_current_request(datagram->payload, datagram->size, &get_value_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        int16_t power_cdbfs = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (get_value_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_measure_power(inventory, get_value_request.channel, 1024, &power_cdbfs,
                                              SCANNER_CAPTURE_TIMEOUT_MS))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        response_size = scanner_encode_power_response(response, get_value_request.request_id,
                                                      SCANNER_MEASURE_CURRENT_COMMAND, status,
                                                      get_value_request.channel, power_cdbfs);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_measure_frequency_request(datagram->payload, datagram->size, &measure_frequency_request))
    {
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t actual_hz = 0;
        int16_t power_cdbfs = 0;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (measure_frequency_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_set_frequency(inventory, measure_frequency_request.channel,
                                              (uint64_t)measure_frequency_request.frequency_khz * 1000u, &actual_hz))
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        else if (!scanner_radio_measure_power(inventory, measure_frequency_request.channel, 1024, &power_cdbfs,
                                              SCANNER_CAPTURE_TIMEOUT_MS))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        response_size = scanner_encode_power_response(response, measure_frequency_request.request_id,
                                                      SCANNER_MEASURE_FREQUENCY_COMMAND, status,
                                                      measure_frequency_request.channel, power_cdbfs);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_sweep_request(datagram->payload, datagram->size, &sweep_request))
    {
        uint32_t frequencies[SCANNER_MAX_SWEEP_POINTS];
        int16_t powers[SCANNER_MAX_SWEEP_POINTS];
        uint16_t count = 0;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        uint64_t point_count;
        uint64_t current_khz;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (sweep_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (sweep_request.step_khz == 0 || sweep_request.start_khz > sweep_request.stop_khz
                 || (uint64_t)sweep_request.start_khz * 1000u < active_frontend->frequency_min_hz
                 || (uint64_t)sweep_request.stop_khz * 1000u > active_frontend->frequency_max_hz)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        point_count = status == SCANNER_RADIO_STATUS_OK
                          ? ((uint64_t)sweep_request.stop_khz - sweep_request.start_khz) / sweep_request.step_khz + 1u
                          : 0u;
        if (status == SCANNER_RADIO_STATUS_OK && point_count > SCANNER_MAX_SWEEP_POINTS)
            status = SCANNER_RADIO_STATUS_OUT_OF_RANGE;
        if (status == SCANNER_RADIO_STATUS_OK && !(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        if (status == SCANNER_RADIO_STATUS_OK)
        {
            for (current_khz = sweep_request.start_khz; current_khz <= sweep_request.stop_khz;
                 current_khz += sweep_request.step_khz)
            {
                uint64_t actual_hz = 0;
                uint32_t rounded_khz;
                if (scanner_radio_worker_current_cancelled(worker))
                {
                    status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
                    count = 0;
                    break;
                }
                frequencies[count] = (uint32_t)current_khz;
                if (!scanner_radio_set_frequency(inventory, sweep_request.channel, current_khz * 1000u, &actual_hz)
                    || !scanner_radio_measure_power(inventory, sweep_request.channel, 256, &powers[count],
                                                    SCANNER_CAPTURE_TIMEOUT_MS))
                {
                    status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
                    count = 0;
                    break;
                }
                rounded_khz = (uint32_t)(actual_hz / 1000u);
                frequencies[count] = rounded_khz;
                count++;
                if (sweep_request.stop_khz - current_khz < sweep_request.step_khz)
                    break;
            }
        }
        response_size = scanner_encode_sweep_response(response, sizeof(response), sweep_request.request_id, status,
                                                      sweep_request.channel, count, frequencies, powers);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_raw_iq_request(datagram->payload, datagram->size, &raw_iq_request))
    {
        uint8_t iq[SCANNER_MAX_RAW_IQ_PAIRS * 4u];
        size_t iq_size = 0;
        uint8_t format = 0;
        uint8_t status = SCANNER_RADIO_STATUS_OK;
        active_frontend = scanner_radio_active(inventory);
        if (active_frontend == NULL || active_frontend->device_handle == NULL)
            status = SCANNER_RADIO_STATUS_NO_ACTIVE_FRONTEND;
        else if (raw_iq_request.channel >= active_frontend->rx_channels)
            status = SCANNER_RADIO_STATUS_INVALID_CHANNEL;
        else if (!(active_frontend->capabilities & SCANNER_RADIO_CAP_RX))
            status = SCANNER_RADIO_STATUS_UNSUPPORTED;
        else if (!scanner_radio_capture_iq(inventory, raw_iq_request.channel, raw_iq_request.complex_pairs, iq,
                                           sizeof(iq), &iq_size, &format, SCANNER_CAPTURE_TIMEOUT_MS))
            status = SCANNER_RADIO_STATUS_CAPTURE_ERROR;
        response_size = scanner_encode_raw_iq_response(response, sizeof(response), raw_iq_request.request_id, status,
                                                       raw_iq_request.channel, format,
                                                       status == SCANNER_RADIO_STATUS_OK ? raw_iq_request.complex_pairs
                                                                                         : 0,
                                                       status == SCANNER_RADIO_STATUS_OK ? iq : NULL,
                                                       status == SCANNER_RADIO_STATUS_OK ? iq_size : 0);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        return 1;
    }

    if (scanner_decode_exit_request(datagram->payload, datagram->size, &exit_request))
    {
        response_size = scanner_encode_exit_response(response, exit_request.request_id, SCANNER_RADIO_STATUS_OK);
        if (!store_worker_response(worker_result, response, response_size))
            return 0;
        worker_result->close_after_send = 1;
        printf("Exit command %" PRIu32 " acknowledged; shutting down\n", exit_request.request_id);
        return 2;
    }

    fprintf(stderr, "Ignoring malformed or unsupported radio command\n");
    return 1;
}

static int wait_for_radio_commands(ScannerUdpSocket *socket_handle, const Options *options, ScannerRadioWorker *worker,
                                   char *error, size_t error_size)
{
    for (;;)
    {
        ScannerDatagram datagram;
        ScannerRadioWorkerResult worker_result;
        int result;
        while (scanner_radio_worker_receive(worker, &worker_result))
        {
            if (worker_result.fatal)
            {
                snprintf(error, error_size, "%s", worker_result.error);
                return 0;
            }
            if (worker_result.size > 0
                && !scanner_udp_send(socket_handle, options->server_address, options->server_port,
                                     worker_result.payload, worker_result.size, error, error_size))
                return 0;
            if (worker_result.close_after_send)
                return 1;
        }

        result = scanner_udp_receive(socket_handle, 20, &datagram, error, error_size);
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
        {
            ScannerExitRequest exit_request;
            if (scanner_decode_exit_request(datagram.payload, datagram.size, &exit_request))
                scanner_radio_worker_cancel_for_exit(worker);
        }
        if (!scanner_radio_worker_submit(worker, &datagram))
        {
            snprintf(error, error_size, "%s", "Radio worker request queue is full or stopping");
            return 0;
        }
    }
}

static int run_node(const Options *options)
{
    ScannerDevice device;
    ScannerRadioInventory radio_inventory;
    ScannerRadioWorker *radio_worker = NULL;
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
    if (!scanner_radio_worker_start(&radio_worker, &radio_inventory, handle_radio_command, (void *)options, error,
                                    sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        result = 0;
        goto cleanup;
    }
    if (!wait_for_radio_commands(&socket_handle, options, radio_worker, error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        result = 0;
    }
    else
    {
        printf("Exit command completed\n");
    }

cleanup:
    if (radio_worker != NULL)
        scanner_radio_worker_stop(&radio_worker);
    else
        scanner_radio_close_all(&radio_inventory);
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