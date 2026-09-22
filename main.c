#include <stdint.h>
const uint32_t ver = 2401103;
const uint8_t debug = 1; // permission for out debug information
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>

// #include <math.h>
#include "vlad-dev.h"
#include <errno.h>
#include <time.h>

#define UDP_BUFFER_SIZE 1100
#define SERVER_IP "82.165.20.164"
// #define my_IP          "127.0.0.1"
// #define SERVER_PORT        2222
uint16_t PORT = 2200;
#define CLIENT_PORT 3333
#define HANDSHAKE_TIMEOUT 2
#define tryConnectToServer 5
#define COMMAND_VER 0x01
#define COMMAND_ERROR 0xFF
#define STATUS_INVALID_PACKET 0x01
#define STATUS_UNKNOWN_COMMAND 0x02
#define REQUEST_ID_SIZE 4
#define VERSION_SIZE 10
uint32_t BAD = 0;
uint32_t GOOD = 0;
uint32_t SENDED = 0;

struct timeval timeTMP;
void cl(char *text)
{
    if (debug)
    {
        gettimeofday(&timeTMP, NULL);
        printf("%ld.%ld %s\n", timeTMP.tv_sec, timeTMP.tv_usec, text);
    }
}

#pragma pack(push, 1)
typedef struct
{
    uint32_t cnt;  // counter
    long int TSS;  // timestamp
    long int TSN;  // timestamp
    uint16_t port; // UDP PORT for response
    Device dev;    // структура с описанием девайса из джейсона
    //    keyPublic;
} HandShake;
#pragma pack(pop)

double outcounter = 0;

///////////////////////////////////////////////////////////////
////////////     UDP         //////////////////////////////////
///////////////////////////////////////////////////////////////

// маленькая обертка на отправку данных по удп
int udp_send(int sock, const unsigned char *buffer, size_t size, const struct sockaddr_in *addr)
{
    ssize_t sent = sendto(sock, buffer, size, 0, (const struct sockaddr *)addr, sizeof(*addr));
    if (sent < 0)
    {
        perror("sendto");
        cl("!! error sendto");
        return -1;
    }
    if ((size_t)sent != size)
    {
        fprintf(stderr, "Отправлен неполный UDP-пакет\n");
        cl("!! error: Отправлен неполный UDP-пакет");
        return -1;
    }
    return 0;
} // end udp-send()

// request_id is always encoded in network byte order (big-endian).
// To switch quickly to little-endian, replace ntohl/htonl below with
// a direct memcpy or the required host-specific conversion.
static uint32_t read_request_id(const unsigned char *buffer)
{
    uint32_t network_id;
    memcpy(&network_id, buffer, sizeof(network_id));
    return ntohl(network_id);
}

static void write_request_id(unsigned char *buffer, uint32_t request_id)
{
    uint32_t network_id = htonl(request_id);
    memcpy(buffer, &network_id, sizeof(network_id));
}

// Successful VER response: [request_id: 4][version: 10].
// The 32-bit version is right-aligned in the 10-byte field and the
// six leading bytes are zero padding.
static size_t build_command_response(const unsigned char *request, size_t request_size,
                                     unsigned char *response)
{
    uint32_t request_id;

    if (request_size < REQUEST_ID_SIZE)
        return 0;

    request_id = read_request_id(request);
    write_request_id(response, request_id);

    if (request_size != REQUEST_ID_SIZE + 1)
    {
        response[REQUEST_ID_SIZE] = COMMAND_ERROR;
        response[REQUEST_ID_SIZE + 1] = STATUS_INVALID_PACKET;
        return REQUEST_ID_SIZE + 2;
    }

    if (request[REQUEST_ID_SIZE] != COMMAND_VER)
    {
        response[REQUEST_ID_SIZE] = COMMAND_ERROR;
        response[REQUEST_ID_SIZE + 1] = STATUS_UNKNOWN_COMMAND;
        return REQUEST_ID_SIZE + 2;
    }

    memset(response + REQUEST_ID_SIZE, 0, VERSION_SIZE);
    uint32_t network_version = htonl(ver);
    memcpy(response + REQUEST_ID_SIZE + VERSION_SIZE - sizeof(network_version), &network_version,
           sizeof(network_version));
    return REQUEST_ID_SIZE + VERSION_SIZE;
}

/////////////////////////////////////////////////////////
// ОСНОВНАЯ ФУНКЦИЯ UDP-отправки заголовочного пакета
/////////////////////////////////////////////////////////
int sendHS(const HandShake *dev)
{
    unsigned char buffer[UDP_BUFFER_SIZE];
    // Копируем Device в буфер
    memcpy(buffer, dev, sizeof(HandShake));

    struct sockaddr_in local_addr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0 ANY
    local_addr.sin_port = htons(CLIENT_PORT);
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("bind");
        close(sock);
        return 1;
    }
    printf("UDP socket is listening on 0.0.0.0:%d\n", CLIENT_PORT);
    // АДРЕС СЕРВЕРА
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != 1)
    {
        fprintf(stderr, "Некорректный IP-адрес\n");
        cl("Некорректный IP-адрес");
        close(sock);
        return -1;
    }

    // ПОВТОРНЫЕ ПОПЫТКИ
    int tryCount = 0;
    cl("? try connect to server:");
    while (tryCount < tryConnectToServer)
    {
        tryCount++;
        printf("Попытка подключения %d из %d\n", tryCount, tryConnectToServer);
        // Отправляем HS
        SENDED++;
        if (udp_send(sock, buffer, sizeof(HandShake), &server_addr) != 0)
        {
            fprintf(stderr, "Ошибка отправки Device\n");
            cl("! Error of send HS");
            close(sock);
            return -1;
        }
        // Ждём ответ 2 секунды
        struct timeval timeout;
        timeout.tv_sec = HANDSHAKE_TIMEOUT;
        timeout.tv_usec = 0;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            perror("setsockopt");
            cl("! error setsockopt");
            close(sock);
            return -1;
        }
        unsigned char response[UDP_BUFFER_SIZE];
        struct sockaddr_in response_addr;
        socklen_t response_addr_len = sizeof(response_addr);
        ssize_t received = recvfrom(sock, response, sizeof(response), 0,
                                    (struct sockaddr *)&response_addr, &response_addr_len);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("Ответ не получен за 2 секунды\n");

                if (tryCount >= tryConnectToServer)
                {
                    BAD++;
                    printf("Превышено число попыток подключения\n");
                    close(sock);
                    return -1;
                }
                printf("Повторяем попытку...\n");
                BAD++;
                continue;
            }
            perror("recvfrom");
            close(sock);
            return -1;
        }
        // ОТВЕТ ПОЛУЧЕН
        GOOD++;
        printf("Сервер ответил. Получено %zd байт\n", received);

        unsigned char command_response[REQUEST_ID_SIZE + 1 + VERSION_SIZE];
        size_t command_response_size = build_command_response(response, (size_t)received,
                                                              command_response);
        if (command_response_size == 0)
        {
            fprintf(stderr, "Некорректный пакет команды: request_id отсутствует\n");
            close(sock);
            return -1;
        }
        if (udp_send(sock, command_response, command_response_size, &response_addr) != 0)
        {
            fprintf(stderr, "Ошибка отправки ответа на команду\n");
            close(sock);
            return -1;
        }
        printf("Ответ на команду отправлен: %zu байт\n", command_response_size);
        break;
    }
    // ДАЛЬНЕЙШИЙ ДИАЛОГ
    printf("Переходим в режим диалога\n");
    close(sock);

    return 0;
} /// end of function

////////////   Main   ///////////////////////////
//////////////   Main   /////////////////////////
////////////   Main   ///////////////////////////
//////////// Main   /////////////////////////////

int main(int argc, char *argv[])
{
    cl("* * * Start");
    // обрабатываем аргументы командной строки
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0)
        {
            if (i + 1 < argc)
            {
                PORT = atoi(argv[i + 1]);
                i++;
            }
            else
            {
                fprintf(stderr, "Ошибка: после флага -p должно идти значение порта.\n");
                return 1;
            }
        }
    }
    cl("i Server Port:");
    printf("PORT:%d\n", PORT);

    ///////////////////////////////////////////////////////////////
    // 1. Создаём и инициализируем Device так просто что бы был один
    ///////////////////////////////////////////////////////////////
    /*
        Device dev = {0};
        dev.id = 12345;
        dev.version = 260911.01;
        dev.coord.lon = 2.154007;
        dev.coord.lat = 41.387400;
    //от фонаря
        for (int i = 0; i < 16; i++){
            dev.rfin[i].in = i;
            dev.rfin[i].from = i * 10;
            dev.rfin[i].to = i * 10 + 9;
            dev.rfin[i].polar = i * 22.5;
            dev.rfin[i].type = i % 6;
            dev.rfin[i].dBi = 10 + i;
            dev.rfin[i].direction = i * 22.5;
        }
    cl("# наполнили фонаревыми данными ");

    */

    // просто для проверки
    ///////////////////////////////////////////////////////////////
    // 2. и Сохраняем этот  Device в джейсон-файл так просто для проверки
    // что алгоритм работает. єто можно из кода вообще убрать
    ///////////////////////////////////////////////////////////////
    //    if (!device_save_json("device.json", &dev)) {
    //        cl("Ошибка сохранения Device");
    //        return 1;
    //    } else cl ("Device сохранён в ini'fail в JSON.");

    ///////////////////////////////////////////////////////////////
    // 3. Создаём ДРУГОЙ Device в памяти "fromINI" считівая его из ини-файла

    Device fromINI = { 0 };

    // 4. Загружаем в него данные из JSON для проверки
    ///////////////////////////////////////////////////////////////
    if (!device_load_json("device.json", &fromINI))
    {
        cl("Ошибка загрузки Device");
        return 1;
    }
    ///////////////////////////////////////////////////////////////
    // 5. если хотим то можем проверить что мы начитали из джейсона:
    ///////////////////////////////////////////////////////////////

    //    printf("\nLoaded Device:\n");
    //    printf("id      = %d\n", fromINI.id);
    //    printf("version = %.2f\n", fromINI.version);
    //    printf("lon     = %.6f\n", fromINI.coord.lon);
    //    printf("lat     = %.6f\n", fromINI.coord.lat);
    //    for (int i = 0; i < 16; i++) {
    //        printf("\nAntenna[%d]:\n",i);
    //        printf("in        = %d\n",fromINI.rfin[i].in);
    //        printf("from      = %d\n", fromINI.rfin[i].from);
    //        printf("to        = %d\n", fromINI.rfin[i].to);
    //        printf("polar     = %.2f\n",fromINI.rfin[i].polar);
    //        printf("type      = %d\n", fromINI.rfin[i].type);
    //        printf("dBi       = %d\n", fromINI.rfin[i].dBi);
    //        printf("direction = %.2f\n",fromINI.rfin[i].direction);
    //    }
    ///////////////////////////////////////////////////////////////
    // 6. Убеждаемся что все ОК!!!
    ///////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////
    // 7. теперь создаем хендшейк
    // сначала чистый буфер как есть с информацией из ини-файла:
    // ту чтопрочитали в переменную fromINI с номером и таймстампом
    ///////////////////////////////////////////////////////////////

    //    struct timecpec ts;
    //    timespec_get(&ts, TIME_UTC);

    //    long long ts_ms=((long long)ts.tv_sec*1000)+(ts.tv_nsec/1000000);
    //    printf("ts_ms=%lldms\n",ts_ms) ;

    //*********************************//
    gettimeofday(&timeTMP, NULL);

    HandShake hsOriginal = { .cnt = outcounter,
                             .TSS = timeTMP.tv_sec,
                             .TSN = timeTMP.tv_usec,
                             .port = CLIENT_PORT,
                             .dev = fromINI };
    //***********************************
    cl("i HandShake ready to send");
    printf("size HS= %zu\n", sizeof(hsOriginal));

    //////***************************************************************///

    gettimeofday(&timeTMP, NULL);
    long long int startTime = timeTMP.tv_sec * 1000000 + timeTMP.tv_usec;

    cl(" ------Start loop");
    int LOOP = 3;
    while (LOOP--)
    {
        hsOriginal.cnt++;
        if (sendHS(&hsOriginal) != 0)
        {
            fprintf(stderr, "UDP-диалог завершён c ошибкой\n");
            LOOP = 0;
            //        return 1;
        }
        else
        {
            cl("i HandShake sended and answer recived");
            printf("LOOP:%d\n", LOOP);
        }
    }
    cl(" <-- END TIME");
    printf("\n\nSENDED: %d GOOD:%d BAD:%d\n", SENDED, GOOD, BAD);

    printf("Start:%lld\n", startTime);

    gettimeofday(&timeTMP, NULL);
    long long int endTime = timeTMP.tv_sec * 1000000 + timeTMP.tv_usec;
    printf("End:%lld\n", endTime);
    printf("delta:%lld\n", endTime - startTime);

    // printf ("End time %ld\n", timeTMP.tv_sec*1000000+timeTMP.tv_usec);

    return 0;
} // end of main
