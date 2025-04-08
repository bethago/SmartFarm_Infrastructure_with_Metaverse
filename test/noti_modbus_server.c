#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#define SERVER_IP "192.168.0.69"
#define SERVER_PORT 3002

uint64_t current_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)(ts.tv_sec) * 1000 + (ts.tv_nsec / 1000000);
}

void send_http_post(int value, uint64_t t1) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] connect failed");
        return;
    }

    char body[512];
    snprintf(body, sizeof(body),
        "{\"m2m:sgn\":{\"m2m:nev\":{\"m2m:rep\":{\"m2m:fcnt\":{\"discharging\":%d,\"t1\":%" PRIu64 "}}}}}",
        value, t1);

    char request[1024];
    snprintf(request, sizeof(request),
        "POST /write HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %lu\r\n"
        "\r\n"
        "%s",
        SERVER_IP, SERVER_PORT, strlen(body), body);

    // 요청 전송
    send(sock, request, strlen(request), 0);

    printf("[SENT] discharging=%d at %" PRIu64 " (manual HTTP)\n", value, t1);

    close(sock);
}

int main() {
    int value = 1;
    while (1) {
        uint64_t t1 = current_time_ms();
        send_http_post(value, t1);
        value = 1 - value;
        sleep(2);
    }
    return 0;
}
