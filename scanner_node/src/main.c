#include "analysis_worker.h"
#include "device_config.h"
#include "iq_recording.h"
#include "perf_probe.h"
#include "perf_probe_hal.h"
#include "protocol.h"
#include "radio_commands.h"
#include "radio_worker.h"
#include "scanner_log.h"
#include "scanner_options.h"
#include "scanner_session.h"
#include "udp_socket.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void log_perf_line(const char *line) { scanner_log_write('*', "perf_probe.c", "%s", line); }

static int wait_for_radio_commands(ScannerUdpSocket *socket_handle, const ScannerOptions *options,
                                   ScannerRadioWorker *worker, char *error, size_t error_size)
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
            LG('!', "Ignoring radio command from unexpected endpoint %s", source);
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

static int run_node(const ScannerOptions *options)
{
    ScannerDevice device;
    ScannerRadioInventory radio_inventory;
    ScannerRadioWorker *radio_worker = NULL;
    ScannerAnalysisWorker *analysis_worker = NULL;
    ScannerRadioCommandContext app_context;
    ScannerUdpSocket socket_handle;
    FILE *iq_file = NULL;
    char error[256];
    unsigned int attempt;
    int result = 0;

    memset(&socket_handle, 0, sizeof(socket_handle));
    socket_handle.handle = SCANNER_INVALID_SOCKET;
    perf_init();
    perf_hal_set_log_sink(log_perf_line);
    if (!device_load_json(options->device_json, &device, error, sizeof(error)))
    {
        LGF("Device JSON: %s", error);
        return 0;
    }
    if (options->save_device_json != NULL
        && !device_save_json(options->save_device_json, &device, error, sizeof(error)))
    {
        LGF("Device JSON save: %s", error);
        return 0;
    }

    LG('+', "Device configuration loaded from %s", options->device_json);
    scanner_radio_discover(&radio_inventory);
    LG('i', "Radio frontends detected: %lu", (unsigned long)radio_inventory.count);
    for (attempt = 0; attempt < radio_inventory.count; ++attempt)
    {
        const ScannerRadioFrontend *frontend = &radio_inventory.frontends[attempt];
        LG('i',
           "frontend=%u backend=%s name=%s RX=%u TX=%u range=%" PRIu64 "..%" PRIu64
           "Hz/step=%u sample-rate=%u..%uHz/step=%u bandwidth=%u..%uHz/step=%u gain=%d..%d/step=%d"
           " centi-dB IQ=%u-bit/fmt%u AGC=0x%02x bw-options=%u",
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
        LGF("%s", error);
        return 0;
    }
    if (!scanner_udp_bind(&socket_handle, options->bind_address, options->local_port, error, sizeof(error)))
    {
        LGF("%s", error);
        goto cleanup;
    }

    if (options->iq_file != NULL && !scanner_iq_recording_open(options->iq_file, &iq_file, error, sizeof(error)))
    {
        LGF("IQ recording: %s", error);
        goto cleanup;
    }

    if (!scanner_session_establish(&socket_handle, options, &device, error, sizeof(error)))
    {
        if (error[0] != '\0')
            LGF("%s", error);
        else
            LGF("Server session establishment failed");
        result = 0;
        goto cleanup;
    }
    result = 1;
    socket_handle.log_command_frames = 1;
    LG('i', "Waiting for radio commands; Exit (0x%02x) closes the session", (unsigned int)SCANNER_EXIT_COMMAND);
    if (!scanner_analysis_worker_start(&analysis_worker, error, sizeof(error)))
    {
        LGF("%s", error);
        result = 0;
        goto cleanup;
    }
    app_context.analysis_worker = analysis_worker;
    LG('+', "Analysis worker started");
    app_context.iq_file = iq_file;
    if (!scanner_radio_worker_start(&radio_worker, &radio_inventory, scanner_radio_command_handle, &app_context, error,
                                    sizeof(error)))
    {
        LGF("%s", error);
        result = 0;
        goto cleanup;
    }
    LG('+', "Radio worker started");
    if (!wait_for_radio_commands(&socket_handle, options, radio_worker, error, sizeof(error)))
    {
        LGF("%s", error);
        result = 0;
    }
    else
    {
        LG('+', "Exit command completed");
    }

cleanup:
    if (radio_worker != NULL)
        scanner_radio_worker_stop(&radio_worker);
    else
        scanner_radio_close_all(&radio_inventory);
    if (analysis_worker != NULL)
        scanner_analysis_worker_stop(&analysis_worker);
    LG('+', "Radio and analysis workers stopped; device handles closed");
    perf_report_and_reset();
    scanner_udp_close(&socket_handle);
    scanner_iq_recording_close(&iq_file);
    LG(result == 1 ? '+' : '!', "Node shutdown result=%s", result == 1 ? "OK" : "failed");
    return result == 1;
}

int main(int argc, char **argv)
{
    ScannerOptions options;
    char error[256];
    int result;
    int parsed = scanner_options_parse(argc, argv, &options);
    if (parsed == 2)
        return 0;

    if (!parsed)
    {
        LGF("Invalid command-line arguments; use --help for usage");
        return 2;
    }
    if (!scanner_log_open(options.debug, error, sizeof(error)))
    {
        scanner_log_start(__FILE__, "Scanner node starting debug=%u", options.debug);
        LGF("Logging initialization: %s", error);
        return 1;
    }
    scanner_log_start(__FILE__, "Scanner node starting debug=%u server=%s:%u log=%s", options.debug,
                      options.server_address, (unsigned int)options.server_port,
                      scanner_log_path()[0] ? scanner_log_path() : "none");
    result = run_node(&options) ? 0 : 1;
    scanner_log_close();
    return result;
}