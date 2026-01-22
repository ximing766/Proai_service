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
#include "tuya_protocol.h"
#include "ipc_protocol.h"

#define UART_DEV "/dev/ttyS1" // 根据实际情况修改
#define IPC_PORT 5555

// Global Context
int uart_fd = -1;
int server_fd = -1;
int client_fd = -1;
tuya_parser_t parser;

// --- Helper Functions Declaration ---
void bin2hex(const uint8_t *bin, int len, char *hex);
int hex2bin(const char *hex, uint8_t *bin, int max_len);
int open_uart(const char *dev);
int init_socket_server();
void send_ipc_msg(const char *type, int cmd, const uint8_t *payload_data, int payload_len);
void process_ipc_msg(char *json_buf);
void handle_mcu_frame(tuya_parser_t *p);
void init_system();
void cleanup_system();
void run_event_loop();
void test_json_handling();

// --- Main Function ---
// Main 只要核心逻辑，封装了初始化和事件循环
int main() {
    printf("Starting Slave Service (JSON IPC)...\n");

    // 1. 系统资源初始化 (UART, Socket)
    init_system();

    // 2. 执行一次 JSON 处理测试 (验证功能)
    test_json_handling();

    // 3. 进入主事件循环
    printf("Entering Main Event Loop...\n");
    while (1) {
        run_event_loop();
    }

    // 4. 清理资源 (通常不会运行到这里)
    cleanup_system();
    return 0;
}

// --- Implementation Details ---

void init_system() {
    // 1. Open UART
    // 尝试打开串口，如果失败则提示，但不退出，允许在无串口环境下测试 IPC
    uart_fd = open_uart(UART_DEV); 
    if (uart_fd < 0) {
        printf("[WARN] Failed to open UART %s. Running in Mock Mode.\n", UART_DEV);
    } else {
        printf("[INFO] UART opened successfully: %s\n", UART_DEV);
    }
    
    // 2. Init Socket
    server_fd = init_socket_server();
    if (server_fd < 0) {
        fprintf(stderr, "[FATAL] Failed to init socket server\n");
        exit(1);
    }
    printf("[INFO] Socket Server listening on port %d\n", IPC_PORT);

    // 3. Init Parser
    tuya_parser_init(&parser);
}

void cleanup_system() {
    if (client_fd > 0) close(client_fd);
    if (server_fd > 0) close(server_fd);
    if (uart_fd > 0) close(uart_fd);
}

// 事件循环：处理 Socket 连接、IPC 消息、串口消息
void run_event_loop() {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    
    int max_fd = server_fd;
    FD_SET(server_fd, &read_fds);
    
    if (client_fd > 0) {
        FD_SET(client_fd, &read_fds);
        if (client_fd > max_fd) max_fd = client_fd;
    }
    if (uart_fd > 0) {
        FD_SET(uart_fd, &read_fds);
        if (uart_fd > max_fd) max_fd = uart_fd;
    }

    // 阻塞等待事件
    int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    
    if (activity < 0 && errno != EINTR) {
        perror("select");
        return;
    }

    // 1. 处理新连接
    if (FD_ISSET(server_fd, &read_fds)) {
        int new_socket = accept(server_fd, NULL, NULL);
        if (new_socket >= 0) {
            printf("[INFO] New Master connected\n");
            if (client_fd > 0) close(client_fd); // 仅支持单 Master
            client_fd = new_socket;
        }
    }

    // 2. 处理 IPC 消息 (Master -> Slave)
    if (client_fd > 0 && FD_ISSET(client_fd, &read_fds)) {
        uint32_t net_len;
        int n = recv(client_fd, &net_len, 4, MSG_WAITALL);
        if (n != 4) {
            printf("[INFO] Master disconnected\n");
            close(client_fd);
            client_fd = -1;
        } else {
            uint32_t len = ntohl(net_len);
            char *json_buf = malloc(len + 1);
            if (json_buf) {
                n = recv(client_fd, json_buf, len, MSG_WAITALL);
                if (n != len) {
                    printf("[WARN] Incomplete JSON received\n");
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

// 处理来自 Master 的 JSON 消息
void process_ipc_msg(char *json_buf) {
    // printf("[DEBUG] Recv JSON: %s\n", json_buf);

    cJSON *root = cJSON_Parse(json_buf);
    if (!root) {
        printf("[ERROR] JSON Parse Error\n");
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
            
            if (cmd_item && payload_item && cJSON_IsString(payload_item)) {
                uint8_t cmd = (uint8_t)cmd_item->valueint;
                char *hex = payload_item->valuestring;
                int hex_len = strlen(hex) / 2;
                uint8_t *tx_data = malloc(hex_len);
                hex2bin(hex, tx_data, hex_len);
                
                uint8_t tx_buf[1024];
                int tx_len = tuya_pack_frame(cmd, tx_data, hex_len, tx_buf);
                
                if (uart_fd > 0) {
                    write(uart_fd, tx_buf, tx_len);
                } else {
                    printf("[Mock UART TX] Raw: ");
                    for(int i=0; i<tx_len; i++) printf("%02X ", tx_buf[i]);
                    printf("\n");
                }
                free(tx_data);
            }
        }
    } 

    cJSON_Delete(root);
}

// 简单的 JSON 处理测试
void test_json_handling() {
    printf("--- Running JSON Handling Test ---\n");
    // 模拟一个来自 Master 的 send_mcu 消息 (payload 模式)
    const char *test_json = "{\"type\": \"send_mcu\", \"data\": {\"cmd\": 6, \"payload\": \"0101000101\"}}";
    
    // 这里的 buf 需要是可写的，因为某些 JSON 解析器可能会修改字符串(cJSON一般不会，但为了安全)
    char buf[256];
    strncpy(buf, test_json, sizeof(buf));
    
    printf("Test Input: %s\n", buf);
    process_ipc_msg(buf); // 应该会触发 [Mock UART TX]
    printf("--- JSON Test Finished ---\n");
}

// 辅助函数实现
void bin2hex(const uint8_t *bin, int len, char *hex) {
    for (int i = 0; i < len; i++) {
        sprintf(hex + i * 2, "%02X", bin[i]);
    }
    hex[len * 2] = '\0';
}

int hex2bin(const char *hex, uint8_t *bin, int max_len) {
    int len = strlen(hex);
    if (len % 2 != 0) return -1;
    int bin_len = len / 2;
    if (bin_len > max_len) bin_len = max_len;
    
    for (int i = 0; i < bin_len; i++) {
        sscanf(hex + i * 2, "%02hhX", &bin[i]);
    }
    return bin_len;
}

int open_uart(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        // perror("open_uart"); // 失败是预期的，如果设备不存在
        return -1;
    }
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

int init_socket_server() {
    // 1. 创建 Socket (AF_INET = IPv4, SOCK_STREAM = TCP)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    // 2. 设置端口复用 (避免重启时端口被占用)
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    addr.sin_port = htons(IPC_PORT);   // 端口号

    // 3. 绑定端口
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    // 4. 开始监听
    if (listen(fd, 1) < 0) {
        perror("listen");
        return -1;
    }
    return fd;
}

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
    printf("Recv MCU Frame: Cmd=0x%02X Len=%d\n", p->cmd, p->data_len);
    send_ipc_msg(IPC_TYPE_EVT_MCU, p->cmd, p->data_buf, p->data_len);
}
