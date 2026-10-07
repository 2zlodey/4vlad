#define _POSIX_C_SOURCE 200809L
#include "scanner_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK log_mutex = SRWLOCK_INIT;
#define LOG_LOCK() AcquireSRWLockExclusive(&log_mutex)
#define LOG_UNLOCK() ReleaseSRWLockExclusive(&log_mutex)
#else
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOG_LOCK() ((void)pthread_mutex_lock(&log_mutex))
#define LOG_UNLOCK() ((void)pthread_mutex_unlock(&log_mutex))
#endif

static unsigned int log_mode;
static int log_enabled;
static FILE *log_file;
static char log_filename[160];

static int local_timestamp(char *output, size_t capacity, const char *format)
{
    time_t now = time(NULL);
    struct tm parts;
#ifdef _WIN32
    struct tm *local = localtime(&now);
    if (local == NULL)
        return 0;
    parts = *local;
#else
    if (localtime_r(&now, &parts) == NULL)
        return 0;
#endif
    return strftime(output, capacity, format, &parts) != 0;
}

static const char *module_name(const char *module)
{
    const char *name = module;
    const char *position;
    for (position = module; *position != '\0'; ++position)
        if (*position == '/' || *position == '\\')
            name = position + 1;
    return name;
}

static void write_text(FILE *destination, const char *text, int html)
{
    const unsigned char *position;
    for (position = (const unsigned char *)text; *position; ++position)
    {
        if (*position == '\n' || *position == '\r')
            fputc(' ', destination);
        else if (html && *position == '&')
            fputs("&amp;", destination);
        else if (html && *position == '<')
            fputs("&lt;", destination);
        else if (html && *position == '>')
            fputs("&gt;", destination);
        else if (html && *position == '"')
            fputs("&quot;", destination);
        else
            fputc(*position, destination);
    }
}

static void write_line(FILE *destination, int html, char marker, const char *module, const char *message)
{
    char timestamp[40] = "unknown-date";
    char prefix[256];
    local_timestamp(timestamp, sizeof(timestamp), "%Y-%m-%d:%H:%M:%S");
    snprintf(prefix, sizeof(prefix), "%s %c [%s] ", timestamp, marker, module_name(module));
    write_text(destination, prefix, html);
    write_text(destination, message, html);
    fputs(html ? "<br>\n" : "\n", destination);
    fflush(destination);
}

static void emit(char marker, const char *module, const char *format, va_list arguments, int forced)
{
    char message[8192];
    LOG_LOCK();
    if (!log_enabled && !forced)
    {
        LOG_UNLOCK();
        return;
    }
    vsnprintf(message, sizeof(message), format, arguments);
    if (forced || (log_enabled && log_mode == 1))
        write_line(forced == 2 ? stderr : stdout, 0, marker, module, message);
    if (log_enabled && log_file != NULL)
    {
        write_line(log_file, log_mode == 3, marker, module, message);
        if (ferror(log_file))
        {
            write_line(stderr, 0, '!', "scanner_log.c", "Fatal log file write failure");
            fclose(log_file);
            log_file = NULL;
            log_mode = 0;
        }
    }
    LOG_UNLOCK();
}

int scanner_log_open(unsigned int debug, char *error, size_t error_size)
{
    char timestamp[40];
    unsigned long process_id;
    static unsigned int sequence;
    scanner_log_close();
    if (debug > 3)
    {
        snprintf(error, error_size, "debug must be 0..3");
        return 0;
    }
    LOG_LOCK();
    log_mode = debug;
    log_filename[0] = '\0';
    if (debug >= 2)
    {
#ifdef _WIN32
        process_id = (unsigned long)GetCurrentProcessId();
#else
        process_id = (unsigned long)getpid();
#endif
        if (!local_timestamp(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S"))
        {
            snprintf(error, error_size, "Cannot obtain log timestamp");
            LOG_UNLOCK();
            return 0;
        }
        snprintf(log_filename, sizeof(log_filename), "scanner_%s_%lu_%u.%s", timestamp, process_id, sequence++,
                 debug == 3 ? "html" : "txt");
        log_file = fopen(log_filename, "a");
        if (log_file == NULL)
        {
            snprintf(error, error_size, "Cannot open log file %s", log_filename);
            LOG_UNLOCK();
            return 0;
        }
    }
    log_enabled = 1;
    LOG_UNLOCK();
    return 1;
}

void scanner_log_close(void)
{
    LOG_LOCK();
    if (log_file != NULL)
        fclose(log_file);
    log_file = NULL;
    log_enabled = 0;
    LOG_UNLOCK();
}

const char *scanner_log_path(void) { return log_filename; }

void scanner_log_write(char marker, const char *module, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    emit(marker, module, format, arguments, 0);
    va_end(arguments);
}

void scanner_log_start(const char *module, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    emit('i', module, format, arguments, 1);
    va_end(arguments);
}

void scanner_log_fatal(const char *module, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    emit('!', module, format, arguments, 2);
    va_end(arguments);
}

void scanner_log_packet(char marker, const char *module, const char *endpoint, const uint8_t *payload, size_t size,
                        int command_frame)
{
    char hex[6145];
    size_t shown = size;
    size_t index;
    unsigned long request_id = 0;
    unsigned int opcode = 0;
    if (command_frame && size >= 5)
    {
        request_id = (unsigned long)payload[0] | ((unsigned long)payload[1] << 8) | ((unsigned long)payload[2] << 16)
                     | ((unsigned long)payload[3] << 24);
        opcode = payload[4];
        if (marker == '>' && opcode == 0x70 && shown > 10)
            shown = 10;
    }
    if (shown > (sizeof(hex) - 1) / 3)
        shown = (sizeof(hex) - 1) / 3;
    for (index = 0; index < shown; ++index)
        snprintf(hex + index * 3, sizeof(hex) - index * 3, "%02x ", payload[index]);
    hex[shown * 3] = '\0';
    if (command_frame && size >= 5)
        scanner_log_write(marker, module, "UDP %s bytes=%lu id=%lu opcode=0x%02x status=%d hex=%s%s", endpoint,
                          (unsigned long)size, request_id, opcode, marker == '>' && size >= 6 ? (int)payload[5] : -1,
                          hex, shown < size ? "[payload omitted]" : "");
    else
        scanner_log_write(marker, module, "UDP %s bytes=%lu hex=%s%s", endpoint, (unsigned long)size, hex,
                          shown < size ? "[payload omitted]" : "");
}