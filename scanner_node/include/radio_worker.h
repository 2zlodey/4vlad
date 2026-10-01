#ifndef SCANNER_NODE_RADIO_WORKER_H
#define SCANNER_NODE_RADIO_WORKER_H

#include "protocol.h"
#include "udp_socket.h"

#include <stddef.h>
#include <stdint.h>

#define SCANNER_RADIO_WORKER_QUEUE_CAPACITY 8u

typedef struct ScannerRadioWorker ScannerRadioWorker;

typedef struct
{
    size_t size;
    uint8_t payload[SCANNER_RADIO_COMMAND_RESPONSE_MAX_SIZE];
    int close_after_send;
    int fatal;
    char error[256];
} ScannerRadioWorkerResult;

typedef int (*ScannerRadioWorkerHandler)(ScannerRadioWorker *worker, ScannerRadioInventory *inventory,
                                         const ScannerDatagram *request, ScannerRadioWorkerResult *result,
                                         void *context);

int scanner_radio_worker_start(ScannerRadioWorker **worker, ScannerRadioInventory *inventory,
                               ScannerRadioWorkerHandler handler, void *context, char *error, size_t error_size);
int scanner_radio_worker_submit(ScannerRadioWorker *worker, const ScannerDatagram *request);
int scanner_radio_worker_receive(ScannerRadioWorker *worker, ScannerRadioWorkerResult *result);
void scanner_radio_worker_cancel_for_exit(ScannerRadioWorker *worker);
int scanner_radio_worker_current_cancelled(ScannerRadioWorker *worker);
void scanner_radio_worker_stop(ScannerRadioWorker **worker);

#endif