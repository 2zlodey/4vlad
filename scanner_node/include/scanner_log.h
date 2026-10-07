#ifndef SCANNER_NODE_LOG_H
#define SCANNER_NODE_LOG_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define SCANNER_LOG_FORMAT(format_index, first_argument) __attribute__((format(printf, format_index, first_argument)))
#else
#define SCANNER_LOG_FORMAT(format_index, first_argument)
#endif

int scanner_log_open(unsigned int debug, char *error, size_t error_size);
void scanner_log_close(void);
const char *scanner_log_path(void);
void scanner_log_write(char marker, const char *module, const char *format, ...) SCANNER_LOG_FORMAT(3, 4);
void scanner_log_start(const char *module, const char *format, ...) SCANNER_LOG_FORMAT(2, 3);
void scanner_log_fatal(const char *module, const char *format, ...) SCANNER_LOG_FORMAT(2, 3);
void scanner_log_packet(char marker, const char *module, const char *endpoint, const uint8_t *payload, size_t size,
                        int command_frame);

#define LG(marker, ...) scanner_log_write(marker, __FILE__, __VA_ARGS__)
#define LGF(...) scanner_log_fatal(__FILE__, __VA_ARGS__)

#endif