#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

#define LOG_I(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_E(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

int send_json(int sock_fd, const char *json_msg) {
    uint32_t len = strlen(json_msg);
    uint32_t net_len = htonl(len);

    if (send(sock_fd, &net_len, 4, 0) != 4) {
        perror("send header failed");
        return -1;
    }
    if (send(sock_fd, json_msg, len, 0) != len) {
        perror("send body failed");
        return -1;
    }
    LOG_I("Sent: %s", json_msg);
    return 0;
}

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;

    // 1. 创建 Socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket creation failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("invalid address");
        return 1;
    }

    // 2. 连接到 Slave Service
    LOG_I("Connecting to %s:%d...", SERVER_IP, SERVER_PORT);
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        return 1;
    }
    LOG_I("Connected to Slave Service!");

    // 3. 循环发送测试
    const char *msg_with_payload = "{\"type\": \"send_mcu\", \"data\": {\"cmd\": 6, \"payload\": \"0101000101\"}}";
    const char *msg_no_payload   = "{\"type\": \"send_mcu\", \"data\": {\"cmd\": 8}}"; // 状态查询，无payload

    LOG_I("Starting loop test (Interval: 250ms)...");
    
    int count = 0;
    while (1) {
        if (count % 2 == 0) {
            if (send_json(sock_fd, msg_with_payload) < 0) break;
        } else {
            if (send_json(sock_fd, msg_no_payload) < 0) break;
        }
        
        count++;
        usleep(250000); // 休眠 250ms
    }

    close(sock_fd);
    LOG_I("Connection closed.");
    return 0;
}
