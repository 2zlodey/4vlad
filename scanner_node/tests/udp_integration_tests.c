#include "protocol.h"
#include "udp_socket.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message)
{
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", message);

    return condition;
}

int main(void)
{
    ScannerUdpSocket server;
    ScannerUdpSocket client;
    ScannerDatagram packet;
    ScannerDevice device;
    ScannerHandshakeHeader header;
    ScannerVerRequest request;
    uint8_t handshake[SCANNER_HANDSHAKE_SIZE];
    uint8_t session_reply[SCANNER_SESSION_REPLY_SIZE] = { 0 };
    uint8_t ver_request[SCANNER_VER_REQUEST_SIZE] = { 0x78, 0x56, 0x34, 0x12, 0x01 };
    uint8_t ver_response[SCANNER_VER_RESPONSE_SIZE];
    uint16_t server_port;
    uint16_t client_port;
    char error[256];
    int success = 0;

    memset(&server, 0, sizeof(server));
    memset(&client, 0, sizeof(client));
    server.handle = SCANNER_INVALID_SOCKET;
    client.handle = SCANNER_INVALID_SOCKET;
    memset(&device, 0, sizeof(device));

    if (!scanner_udp_open(&server, error, sizeof(error))
        || !scanner_udp_bind(&server, "127.0.0.1", 0, error, sizeof(error))
        || !scanner_udp_get_local_port(&server, &server_port, error, sizeof(error))
        || !scanner_udp_open(&client, error, sizeof(error))
        || !scanner_udp_bind(&client, "127.0.0.1", 0, error, sizeof(error))
        || !scanner_udp_get_local_port(&client, &client_port, error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }

    memset(&header, 0, sizeof(header));
    header.counter = 1;
    header.reply_port = client_port;
    scanner_encode_handshake(handshake, &header, &device);
    if (!scanner_udp_send(&client, "127.0.0.1", server_port, handshake, sizeof(handshake), error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }
    if (!check(scanner_udp_receive(&server, 1000, &packet, error, sizeof(error)) == 1,
               "Server did not receive handshake")
        || !check(packet.size == SCANNER_HANDSHAKE_SIZE, "Handshake size mismatch")
        || !check(packet.payload[0] == 1 && packet.payload[1] == 0, "Handshake content mismatch"))
    {
        goto cleanup;
    }

    if (!scanner_udp_send(&server, "127.0.0.1", client_port, session_reply, sizeof(session_reply), error, sizeof(error))
        || !scanner_udp_send(&server, "127.0.0.1", client_port, ver_request, sizeof(ver_request), error, sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }
    if (!check(scanner_udp_receive(&client, 1000, &packet, error, sizeof(error)) == 1
                   && packet.size == SCANNER_SESSION_REPLY_SIZE,
               "Client did not receive session response")
        || !check(scanner_udp_receive(&client, 1000, &packet, error, sizeof(error)) == 1
                      && scanner_decode_ver_request(packet.payload, packet.size, &request),
                  "Client did not decode server VER request"))
    {
        goto cleanup;
    }

    if (!scanner_encode_ver_response(ver_response, request.request_id, "1.0.0.0")
        || !scanner_udp_send(&client, "127.0.0.1", server_port, ver_response, sizeof(ver_response), error,
                             sizeof(error)))
    {
        fprintf(stderr, "%s\n", error);
        goto cleanup;
    }
    if (!check(scanner_udp_receive(&server, 1000, &packet, error, sizeof(error)) == 1,
               "Server did not receive VER response")
        || !check(packet.size == SCANNER_VER_RESPONSE_SIZE, "VER response size mismatch")
        || !check(packet.payload[0] == 0x78 && packet.payload[3] == 0x12, "VER RequestId mismatch")
        || !check(memcmp(packet.payload + 4, "1.0.0.0", 7) == 0, "VER version mismatch"))
    {
        goto cleanup;
    }
    success = 1;
    puts("Handshake/session/VER UDP exchange passed.");

cleanup:
    scanner_udp_close(&client);
    scanner_udp_close(&server);
    return success ? 0 : 1;
}