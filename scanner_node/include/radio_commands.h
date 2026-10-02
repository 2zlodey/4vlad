#ifndef SCANNER_NODE_RADIO_COMMANDS_H
#define SCANNER_NODE_RADIO_COMMANDS_H

#include "analysis_worker.h"
#include "radio_worker.h"

#include <stdio.h>

typedef struct
{
    ScannerAnalysisWorker *analysis_worker;
    FILE *iq_file;
} ScannerRadioCommandContext;

int scanner_radio_command_handle(ScannerRadioWorker *worker, ScannerRadioInventory *inventory,
                                 const ScannerDatagram *datagram, ScannerRadioWorkerResult *worker_result,
                                 void *context);

#endif
