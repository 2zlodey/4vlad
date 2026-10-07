#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "scanner_log.h"
#include "udp_socket.h"


#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *operation, int code)
{
    LG('!', "UDP operation %s failed code=%d", operation, code);
    if (error == NULL || error_size == 0)
        return;

#ifdef _WIN32
    snprintf(error, error_size, "%s failed with Winsock error %d", operation, code);
#else
    snprintf(error, error_size, "%s failed: %s", operation, strerror(code));
#endif
}

int scanner_udp_open(ScannerUdpSocket *socket_handle, char *error, size_t error_size)
{
    if (socket_handle == NULL)
    {
        set_error(error, error_size, "socket argument", 0);
        return 0;
    }
    socket_handle->handle = SCANNER_INVALID_SOCKET;
    socket_handle->winsock_started = 0;
    socket_handle->log_command_frames = 0;
#ifdef _WIN32
    {
        WSADATA data;
        int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
        {
            set_error(error, error_size, "WSAStartup", result);
            return 0;
        }
        socket_handle->winsock_started = 1;
        socket_handle->handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle->handle == INVALID_SOCKET)
        {
            set_error(error, error_size, "socket", WSAGetLastError());
            scanner_udp_close(socket_handle);
            return 0;
        }
    }
#else
    socket_handle->handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handle->handle < 0)
    {
        set_error(error, error_size, "socket", errno);
        return 0;
    }
#endif
    LG('+', "UDP socket opened");
    return 1;
}

static int make_endpoint(const char *address, uint16_t port, struct sockaddr_in *endpoint, char *error,
                         size_t error_size)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->sin_family = AF_INET;
    endpoint->sin_port = htons(port);
    if (address == NULL || inet_pton(AF_INET, address, &endpoint->sin_addr) != 1)
    {
        if (error != NULL && error_size > 0)
        {
            snprintf(error, error_size, "Invalid IPv4 address: %s", address == NULL ? "(null)" : address);
            LG('!', "%s", error);
        }
        return 0;
    }
    return 1;
}

int scanner_udp_bind(ScannerUdpSocket *socket_handle, const char *address, uint16_t port, char *error,
                     size_t error_size)
{
    struct sockaddr_in endpoint;
    if (socket_handle == NULL || socket_handle->handle == SCANNER_INVALID_SOCKET
        || !make_endpoint(address, port, &endpoint, error, error_size))
    {
        return 0;
    }
    if (bind(socket_handle->handle, (const struct sockaddr *)&endpoint, (int)sizeof(endpoint)) != 0)
    {
#ifdef _WIN32
        set_error(error, error_size, "bind", WSAGetLastError());
#else
        set_error(error, error_size, "bind", errno);
#endif
        return 0;
    }
    LG('+', "UDP socket bound to %s:%u", address, (unsigned int)port);
    return 1;
}

int scanner_udp_get_local_port(ScannerUdpSocket *socket_handle, uint16_t *port, char *error, size_t error_size)
{
    struct sockaddr_in endpoint;
#ifdef _WIN32
    int endpoint_size = (int)sizeof(endpoint);
#else
    socklen_t endpoint_size = (socklen_t)sizeof(endpoint);
#endif
    if (socket_handle == NULL || port == NULL || socket_handle->handle == SCANNER_INVALID_SOCKET)
    {
        set_error(error, error_size, "getsockname argument", 0);
        return 0;
    }
    memset(&endpoint, 0, sizeof(endpoint));
    if (getsockname(socket_handle->handle, (struct sockaddr *)&endpoint, &endpoint_size) != 0)
    {
#ifdef _WIN32
        set_error(error, error_size, "getsockname", WSAGetLastError());
#else
        set_error(error, error_size, "getsockname", errno);
#endif
        return 0;
    }
    *port = ntohs(endpoint.sin_port);
    return 1;
}

int scanner_udp_send(ScannerUdpSocket *socket_handle, const char *address, uint16_t port, const uint8_t *data,
                     size_t size, char *error, size_t error_size)
{
    struct sockaddr_in endpoint;
    if (socket_handle == NULL || socket_handle->handle == SCANNER_INVALID_SOCKET || data == NULL || size == 0
        || !make_endpoint(address, port, &endpoint, error, error_size))
    {
        return 0;
    }
#ifdef _WIN32
    {
        int sent;
        if (size > (size_t)INT_MAX)
        {
            set_error(error, error_size, "sendto size", WSAEMSGSIZE);
            return 0;
        }
        sent = sendto(socket_handle->handle, (const char *)data, (int)size, 0, (const struct sockaddr *)&endpoint,
                      (int)sizeof(endpoint));
        if (sent == SOCKET_ERROR || (size_t)sent != size)
        {
            set_error(error, error_size, "sendto", WSAGetLastError());
            return 0;
        }
    }
#else
    {
        ssize_t sent = sendto(socket_handle->handle, data, size, 0, (const struct sockaddr *)&endpoint,
                              sizeof(endpoint));
        if (sent < 0 || (size_t)sent != size)
        {
            set_error(error, error_size, "sendto", errno);
            return 0;
        }
    }
#endif
    {
        char destination[64];
        snprintf(destination, sizeof(destination), "%s:%u", address, (unsigned int)port);
        scanner_log_packet('>', __FILE__, destination, data, size, socket_handle->log_command_frames);
    }
    return 1;
}

int scanner_udp_receive(ScannerUdpSocket *socket_handle, unsigned int timeout_ms, ScannerDatagram *datagram,
                        char *error, size_t error_size)
{
    fd_set read_set;
    struct timeval timeout;
    int ready;
#ifdef _WIN32
    int source_size = (int)sizeof(datagram->source);
#else
    socklen_t source_size = (socklen_t)sizeof(datagram->source);
#endif

    if (socket_handle == NULL || datagram == NULL || socket_handle->handle == SCANNER_INVALID_SOCKET)
    {
        set_error(error, error_size, "recvfrom argument", 0);
        return -1;
    }
    FD_ZERO(&read_set);
    FD_SET(socket_handle->handle, &read_set);
    timeout.tv_sec = (long)(timeout_ms / 1000u);
    timeout.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
#ifdef _WIN32
    ready = select(0, &read_set, NULL, NULL, &timeout);
#else
    ready = select(socket_handle->handle + 1, &read_set, NULL, NULL, &timeout);
#endif
    if (ready == 0)
        return 0;

    if (ready < 0)
    {
#ifdef _WIN32
        set_error(error, error_size, "select", WSAGetLastError());
#else
        set_error(error, error_size, "select", errno);
#endif
        return -1;
    }
#ifdef _WIN32
    {
        int received = recvfrom(socket_handle->handle, (char *)datagram->payload, (int)sizeof(datagram->payload), 0,
                                (struct sockaddr *)&datagram->source, &source_size);
        if (received == SOCKET_ERROR)
        {
            int code = WSAGetLastError();
            if (code == WSAECONNRESET)
                return 0;

            set_error(error, error_size, "recvfrom", code);
            return -1;
        }
        datagram->size = (size_t)received;
    }
#else
    {
        ssize_t received = recvfrom(socket_handle->handle, datagram->payload, sizeof(datagram->payload), 0,
                                    (struct sockaddr *)&datagram->source, &source_size);
        if (received < 0)
        {
            if (errno == ECONNREFUSED)
            {
                return 0;
            }
            set_error(error, error_size, "recvfrom", errno);
            return -1;
        }
        datagram->size = (size_t)received;
    }
#endif
    {
        char source[64];
        scanner_endpoint_string(&datagram->source, source, sizeof(source));
        scanner_log_packet('<', __FILE__, source, datagram->payload, datagram->size, socket_handle->log_command_frames);
    }
    return 1;
}

void scanner_udp_close(ScannerUdpSocket *socket_handle)
{
    if (socket_handle == NULL)
        return;

    if (socket_handle->handle != SCANNER_INVALID_SOCKET)
    {
#ifdef _WIN32
        closesocket(socket_handle->handle);
#else
        close(socket_handle->handle);
#endif
        socket_handle->handle = SCANNER_INVALID_SOCKET;
        LG('+', "UDP socket closed");
    }
#ifdef _WIN32
    if (socket_handle->winsock_started)
    {
        WSACleanup();
        socket_handle->winsock_started = 0;
    }
#endif
}

int scanner_same_endpoint(const struct sockaddr_in *endpoint, const char *address, uint16_t port)
{
    struct sockaddr_in expected;
    return endpoint != NULL && make_endpoint(address, port, &expected, NULL, 0) && endpoint->sin_family == AF_INET
           && endpoint->sin_port == expected.sin_port && endpoint->sin_addr.s_addr == expected.sin_addr.s_addr;
}

void scanner_endpoint_string(const struct sockaddr_in *endpoint, char *output, size_t output_size)
{
    char address[INET_ADDRSTRLEN];
    if (output == NULL || output_size == 0)
        return;

    if (endpoint == NULL || inet_ntop(AF_INET, &endpoint->sin_addr, address, sizeof(address)) == NULL)
    {
        snprintf(output, output_size, "<invalid endpoint>");
        return;
    }
    snprintf(output, output_size, "%s:%u", address, (unsigned int)ntohs(endpoint->sin_port));
}