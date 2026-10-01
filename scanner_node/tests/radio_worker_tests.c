#include "radio_worker.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static int echo_request(ScannerRadioWorker *worker, ScannerRadioInventory *inventory, const ScannerDatagram *request,
                        ScannerRadioWorkerResult *result, void *context)
{
    (void)worker;
    (void)inventory;
    (void)context;
    result->size = request->size;
    memcpy(result->payload, request->payload, request->size);
    return 1;
}

static void short_delay(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec delay = { 0, 1000000L };
    (void)nanosleep(&delay, NULL);
#endif
}

int main(void)
{
    ScannerRadioInventory inventory;
    ScannerRadioWorker *worker = NULL;
    ScannerRadioWorkerResult result;
    ScannerDatagram request;
    char error[256];
    unsigned int attempt;
    int ok = 1;

    memset(&inventory, 0, sizeof(inventory));
    inventory.active_id = SCANNER_RADIO_ID_NONE;
    memset(&request, 0, sizeof(request));
    request.size = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = SCANNER_VER_COMMAND;

    if (!scanner_radio_worker_start(&worker, &inventory, echo_request, NULL, error, sizeof(error)))
    {
        fprintf(stderr, "FAIL: worker start: %s\n", error);
        return 1;
    }
    if (!scanner_radio_worker_submit(worker, &request))
    {
        fprintf(stderr, "FAIL: worker rejected request\n");
        scanner_radio_worker_stop(&worker);
        return 1;
    }

    for (attempt = 0; attempt < 2000 && !scanner_radio_worker_receive(worker, &result); ++attempt)
        short_delay();
    if (attempt == 2000)
    {
        fprintf(stderr, "FAIL: worker did not publish response\n");
        ok = 0;
    }
    else if (result.size != request.size || memcmp(result.payload, request.payload, request.size) != 0 || result.fatal
             || result.close_after_send)
    {
        fprintf(stderr, "FAIL: worker response did not preserve request data\n");
        ok = 0;
    }

    scanner_radio_worker_stop(&worker);
    if (worker != NULL)
    {
        fprintf(stderr, "FAIL: worker handle was not cleared after stop\n");
        ok = 0;
    }
    return ok ? 0 : 1;
}
