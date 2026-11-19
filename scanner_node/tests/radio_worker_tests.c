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

static unsigned int idle_tick(ScannerRadioWorker *worker, ScannerRadioInventory *inventory, void *context)
{
    int *ticks = (int *)context;
    (void)worker;
    (void)inventory;
#ifdef _WIN32
    (void)InterlockedIncrement((volatile LONG *)ticks);
#else
    (void)__sync_add_and_fetch(ticks, 1);
#endif
    return 5u;
}

static int read_ticks(const int *ticks)
{
#ifdef _WIN32
    return (int)InterlockedCompareExchange((volatile LONG *)ticks, 0, 0);
#else
    return __sync_add_and_fetch((int *)ticks, 0);
#endif
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
    int idle_ticks = 0;
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

    if (!scanner_radio_worker_start_with_idle(&worker, &inventory, echo_request, idle_tick, &idle_ticks, error,
                                              sizeof(error)))
    {
        fprintf(stderr, "FAIL: worker start: %s\n", error);
        return 1;
    }
    for (attempt = 0; attempt < 2000 && read_ticks(&idle_ticks) == 0; ++attempt)
        short_delay();
    if (read_ticks(&idle_ticks) == 0)
    {
        fprintf(stderr, "FAIL: worker did not run its idle callback\n");
        scanner_radio_worker_stop(&worker);
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
