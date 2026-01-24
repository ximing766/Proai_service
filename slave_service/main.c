#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "inc/tuya_protocol.h"
#include "inc/ipc_protocol.h"
#include "inc/log.h"
#include "inc/utils.h"

int uart_fd = -1;
int server_fd = -1;
int client_fd = -1;
tuya_parser_t parser;

void send_ipc_msg(const char *type, int cmd, const uint8_t *payload_data, int payload_len);
void process_ipc_msg(char *json_buf);
void handle_mcu_frame(tuya_parser_t *p);
void init_system(int log_to_file);
void cleanup_system();
void run_event_loop();
void test_json_handling();

// BM: main
int main(int argc, char *argv[]) {
    int log_to_file = 0;
    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        log_to_file = 1;
    }
    init_system(log_to_file);

    test_json_handling();
    LOG_I("Entering Main Event Loop...");
    while (1) {
        LOG_I("Main Event Loop Iteration");
        run_event_loop();
    }
    cleanup_system();
    return 0;
}

// BM: Run Event Loop
void run_event_loop() {
    fd_set read_fds;
    FD_ZERO(&read_fds);                 // 1. 清空集合
    
    int max_fd = server_fd;
    FD_SET(server_fd, &read_fds);       // 2. 必选：把 server_fd 放进去 (为了随时响应新连接)
    
    if (client_fd > 0) {                // 3. 可选：如果有客户端连着，把 client_fd 也放进去
        FD_SET(client_fd, &read_fds);
        if (client_fd > max_fd) max_fd = client_fd;
    }
    if (uart_fd > 0) {                  // 4. 可选：如果串口打开了，把 uart_fd 也放进去 
        FD_SET(uart_fd, &read_fds);
        if (uart_fd > max_fd) max_fd = uart_fd;
    }

    // 5. 提交给内核，开始死等 (阻塞)
    int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    
    if (activity < 0 && errno != EINTR) {
        perror("select");
        return;
    }

    // 1. 处理新连接
    if (FD_ISSET(server_fd, &read_fds)) {
        int new_socket = accept(server_fd, NULL, NULL);
        if (new_socket >= 0) {
            LOG_I("New Master connected");
            if (client_fd > 0) close(client_fd); // 仅支持单 Master
            client_fd = new_socket;
        }
    }

    // 2. 处理 IPC 消息 (Master -> Slave)
    if (client_fd > 0 && FD_ISSET(client_fd, &read_fds)) {
        uint32_t net_len;
        int n = recv(client_fd, &net_len, 4, MSG_WAITALL);
        if (n != 4) {
            LOG_I("Master disconnected");
            close(client_fd);
            client_fd = -1;
        } else {
            uint32_t len = ntohl(net_len);
            char *json_buf = malloc(len + 1);
            if (json_buf) {
                n = recv(client_fd, json_buf, len, MSG_WAITALL);
                if (n != len) {
                    LOG_W("Incomplete JSON received");
                } else {
                    json_buf[len] = '\0';
                    process_ipc_msg(json_buf);
                }
                free(json_buf);
            }
        }
    }

    // 3. 处理 UART 消息 (MCU -> Slave)
    if (uart_fd > 0 && FD_ISSET(uart_fd, &read_fds)) {
        uint8_t buf[128];
        int n = read(uart_fd, buf, sizeof(buf));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (tuya_parser_process(&parser, buf[i])) {
                    handle_mcu_frame(&parser);
                }
            }
        }
    }
}

// BM: Process JSON from Master
void process_ipc_msg(char *json_buf) {
    LOG_D("Recv JSON: %s", json_buf);

    cJSON *root = cJSON_Parse(json_buf);
    if (!root) {
        LOG_E("JSON Parse Error");
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type_item)) {
        cJSON_Delete(root);
        return;
    }

    // 场景 A: 发送透传指令 (send_mcu)
    if (strcmp(type_item->valuestring, IPC_TYPE_SEND_MCU) == 0) {
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        if (data_item) {
            cJSON *cmd_item = cJSON_GetObjectItem(data_item, "cmd");
            cJSON *payload_item = cJSON_GetObjectItem(data_item, "payload");
            
            if (cmd_item) {
                uint8_t cmd = (uint8_t)cmd_item->valueint;
                uint8_t *tx_data = NULL;
                int hex_len = 0;

                // 只有当 payload 存在且为字符串时才解析
                if (payload_item && cJSON_IsString(payload_item)) {
                    char *hex = payload_item->valuestring;
                    hex_len = strlen(hex) / 2;
                    if (hex_len > 0) {
                        tx_data = malloc(hex_len);
                        hex2bin(hex, tx_data, hex_len);
                    }
                }
                
                uint8_t tx_buf[1024];
                int tx_len = tuya_pack_frame(cmd, tx_data, hex_len, tx_buf);
                
                char *tx_hex = malloc(tx_len * 2 + 1);
                bin2hex(tx_buf, tx_len, tx_hex);
                
                if (uart_fd > 0) {
                    write(uart_fd, tx_buf, tx_len);
                    LOG_D("UART TX: %s", tx_hex);
                } else {
                    LOG_I("Mock UART TX: %s", tx_hex);
                }
                
                free(tx_hex);
                if (tx_data) free(tx_data);
            }
        }
    } 

    cJSON_Delete(root);
}

void test_json_handling() {
    LOG_I("--- Running JSON Handling Test ---");
    const char *test_json = "{\"type\": \"send_mcu\", \"data\": {\"cmd\": 6, \"payload\": \"0101000101\"}}";
    char buf[256];
    strncpy(buf, test_json, sizeof(buf));
    
    process_ipc_msg(buf);
    LOG_I("--- JSON Test Finished ---");
}

// BM: Send JSON to Master
void send_ipc_msg(const char *type, int cmd, const uint8_t *payload_data, int payload_len) {
    if (client_fd < 0) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "cmd", cmd);
    
    if (payload_len > 0 && payload_data != NULL) {
        char *hex_str = malloc(payload_len * 2 + 1);
        bin2hex(payload_data, payload_len, hex_str);
        cJSON_AddStringToObject(data, "payload", hex_str);
        free(hex_str);
    }
    cJSON_AddItemToObject(root, "data", data);

    char *json_str = cJSON_PrintUnformatted(root);
    uint32_t len = strlen(json_str);
    uint32_t net_len = htonl(len);

    send(client_fd, &net_len, 4, 0);
    send(client_fd, json_str, len, 0);

    free(json_str);
    cJSON_Delete(root);
}

void handle_mcu_frame(tuya_parser_t *p) {
    LOG_I("Recv MCU Frame: Cmd=0x%02X Len=%d", p->cmd, p->data_len);
    send_ipc_msg(IPC_TYPE_EVT_MCU, p->cmd, p->data_buf, p->data_len);
}

void init_system(int log_to_file) {
    log_init(log_to_file);

    uart_fd = open_uart(UART_DEV); 
    if (uart_fd < 0) {
        LOG_W("Failed to open UART %s. Running in Mock Mode.", UART_DEV);
    } else {
        LOG_I("UART opened successfully: %s", UART_DEV);
    }
    
    server_fd = init_socket_server(IPC_PORT);
    if (server_fd < 0) {
        LOG_E("Failed to init socket server");
        exit(1);
    }
    LOG_I("Socket Server listening on port %d", IPC_PORT);

    tuya_parser_init(&parser);
}

void cleanup_system() {
    if (client_fd > 0) close(client_fd);
    if (server_fd > 0) close(server_fd);
    if (uart_fd > 0) close(uart_fd);
    log_close();
}

