#ifndef SCANNER_NODE_SESSION_H
#define SCANNER_NODE_SESSION_H

#include "device_config.h"
#include "scanner_options.h"
#include "udp_socket.h"

#include <stddef.h>

int scanner_session_establish(ScannerUdpSocket *socket_handle, const ScannerOptions *options,
                              const ScannerDevice *device, char *error, size_t error_size);

#endif
