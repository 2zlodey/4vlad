#ifndef SCANNER_NODE_UDP_SOCKET_H
#define SCANNER_NODE_UDP_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ScannerSocket;
#define SCANNER_INVALID_SOCKET INVALID_SOCKET
#else
#include <netinet/in.h>
#include <sys/socket.h>
typedef int ScannerSocket;
#define SCANNER_INVALID_SOCKET (-1)
#endif

typedef struct
{
    uint8_t payload[2048];
    size_t size;
    struct sockaddr_in source;
} ScannerDatagram;

typedef struct
{
    ScannerSocket handle;
    int winsock_started;
} ScannerUdpSocket;

int scanner_udp_open(ScannerUdpSocket *socket_handle, char *error, size_t error_size);
int scanner_udp_bind(ScannerUdpSocket *socket_handle, const char *address, uint16_t port, char *error,
                     size_t error_size);
int scanner_udp_get_local_port(ScannerUdpSocket *socket_handle, uint16_t *port, char *error, size_t error_size);
int scanner_udp_send(ScannerUdpSocket *socket_handle, const char *address, uint16_t port, const uint8_t *data,
                     size_t size, char *error, size_t error_size);
/* Returns 1 for datagram, 0 for timeout/ICMP-unreachable, -1 on error. */
int scanner_udp_receive(ScannerUdpSocket *socket_handle, unsigned int timeout_ms, ScannerDatagram *datagram,
                        char *error, size_t error_size);
void scanner_udp_close(ScannerUdpSocket *socket_handle);
int scanner_same_endpoint(const struct sockaddr_in *endpoint, const char *address, uint16_t port);
void scanner_endpoint_string(const struct sockaddr_in *endpoint, char *output, size_t output_size);

#endif