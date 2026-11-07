#include "scanner_options.h"

#include <stdio.h>

static int require_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void)
{
    ScannerOptions options;
    char *defaults[] = { "scanner_node" };
    char *custom_local[] = { "scanner_node", "--local-port", "4100" };
    char *explicit_reply[] = { "scanner_node", "--local-port", "3333", "--reply-port", "47385" };
    char *reply_before_local[] = { "scanner_node", "--reply-port", "47385", "--local-port", "3333" };
    int ok = 1;

    ok &= require_true(scanner_options_parse(1, defaults, &options) && options.local_port == 3333
                           && options.reply_port == 3333,
                       "Default reply port did not match the local port");
    ok &= require_true(scanner_options_parse(3, custom_local, &options) && options.local_port == 4100
                           && options.reply_port == 4100,
                       "Changing the local port did not update the default reply port");
    ok &= require_true(scanner_options_parse(5, explicit_reply, &options) && options.local_port == 3333
                           && options.reply_port == 47385,
                       "Explicit reply port was not parsed");
    ok &= require_true(scanner_options_parse(5, reply_before_local, &options) && options.local_port == 3333
                           && options.reply_port == 47385,
                       "Local port overwrote an explicitly provided reply port");

    return ok ? 0 : 1;
}
