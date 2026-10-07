#include "scanner_log.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned int mode;
    char error[256];
    char path[200];
    char data[4096];
    uint8_t packet[20] = {
        1, 0, 0, 0, 0x70, 0, 0, 1, 5, 0, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0xab, 0xcd, 0xef, 0xff
    };
    for (mode = 0; mode <= 3; ++mode)
    {
        FILE *file;
        size_t size;
        if (!scanner_log_open(mode, error, sizeof(error)))
            return 1;
        snprintf(path, sizeof(path), "%s", scanner_log_path());
        LG('i', "test <value> & text");
        scanner_log_packet('>', __FILE__, "127.0.0.1:2653", packet, sizeof(packet), 1);
        scanner_log_close();
        if (mode < 2)
        {
            if (path[0] != '\0')
                return 2;
            continue;
        }
        file = fopen(path, "rb");
        if (file == NULL)
            return 3;
        size = fread(data, 1, sizeof(data) - 1, file);
        fclose(file);
        remove(path);
        data[size] = '\0';
        if (strstr(data, "[log_tests.c]") == NULL || strstr(data, "opcode=0x70") == NULL
            || strstr(data, "[payload omitted]") == NULL || strstr(data, "aa bb") != NULL)
            return 4;
        if (mode == 2 && (strstr(data, "test <value> & text") == NULL || strstr(data, "<br>") != NULL))
            return 5;
        if (mode == 3
            && (strstr(data, "test &lt;value&gt; &amp; text<br>") == NULL
                || strstr(data, " &gt; [log_tests.c]") == NULL))
            return 6;
    }
    return scanner_log_open(4, error, sizeof(error)) ? 7 : 0;
}