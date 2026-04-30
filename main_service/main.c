#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>
#include "inc/log.h"
#include "inc/cloud_llm.h"
#include "inc/tuya_protocol.h"

static int g_uart_fd = -1;
static pthread_t g_uart_thread;

// 初始化系统
void init_system(int log_to_file);
void cleanup_system();
void run_event_loop();
int init_uart(const char *dev);
void *uart_rx_thread(void *arg);
void tuya_send_cmd(uint8_t cmd, uint8_t *data, uint16_t len);

// 主函数
int main(int argc, char *argv[]) {
    int log_to_file = 1; // 默认开启文件日志
    if (argc > 1 && strcmp(argv[1], "-s") == 0) { // 如果传入 -s 则仅输出到终端 (stdout)
        log_to_file = 0;
    }

    init_system(log_to_file);

    LOG_I("Entering Main Event Loop...");
    while (1) {
        run_event_loop();
    }
    
    cleanup_system();
    return 0;
}

// 主事件循环
void run_event_loop() {
    static int loop_count = 0;
    sleep(15);
    loop_count++;

    // 每 15 秒发送一次心跳包
    LOG_I("Send Heartbeat to MCU...");
    tuya_send_cmd(CMD_HEARTBEAT, NULL, 0);

    LOG_I("Testing AI Platform: Sending Text Message...");
    cloud_llm_send_text("你好，请介绍一下你自己。");


    // 每 45 秒进行一次 JSON/IOT 测试 (每 3 次心跳)
    if (loop_count % 3 == 0) {
        LOG_I("Testing AI Platform: Sending JSON IOT Descriptor...");
        const char *iot_json = "{\"type\":\"iot\",\"descriptors\":[{\"device\":\"heater_001\",\"method\":\"set_temperature\",\"description\":\"Set heater temperature\",\"parameters\":{\"temp\":\"int\"}}]}";
        cloud_llm_send_json(iot_json);
    }

    // 模拟测试发送指令到 MCU (每 60 秒)
    if (loop_count % 4 == 0) {
        LOG_I("Mock AI Command: Sending DP Command to MCU...");
        uint8_t dp_data[] = {0x01, 0x01, 0x00, 0x01, 0x01}; 
        tuya_send_cmd(CMD_DP_SEND, dp_data, sizeof(dp_data));
    }
}

// 系统初始化
void init_system(int log_to_file) {
    log_init(log_to_file);
    log_set_level(LOG_LEVEL_INFO); // 默认设置为 INFO 级别，过滤 DEBUG 日志

    LOG_I("System Starting...");

    // 1. 初始化 UART 连接兔子板模拟器
    if (init_uart("/tmp/ttyModule") != 0) {
        LOG_W("UART init failed. Running without MCU connection.");
    } else {
        // 刚启动时，发送查询产品信息和 DP 状态查询
        LOG_I("Send Product Info Query to MCU...");
        tuya_send_cmd(CMD_PRODUCT_INFO, NULL, 0);
        
        LOG_I("Send DP Status Query to MCU...");
        tuya_send_cmd(CMD_DP_QUERY, NULL, 0);
    }

    // 2. 使用官方提供的公网测试设备凭据初始化 AI
    const char *test_device_id = "0001";
    const char *test_device_secret = "K2JJTF9SWL4NWWK28DRP7W9YAX4FSRAQ";
    
    if (cloud_llm_init(test_device_id, test_device_secret) != 0) {
        LOG_W("Failed to init AI Platform");
    }
}

// 系统清理
void cleanup_system() {
    if (g_uart_fd > 0) close(g_uart_fd);
    cloud_llm_cleanup();
    log_close();
}

int init_uart(const char *dev) {
    g_uart_fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_uart_fd < 0) return -1;
    
    struct termios options;
    tcgetattr(g_uart_fd, &options);
    cfmakeraw(&options);
    tcsetattr(g_uart_fd, TCSANOW, &options);

    pthread_create(&g_uart_thread, NULL, uart_rx_thread, NULL);
    return 0;
}

void tuya_send_cmd(uint8_t cmd, uint8_t *data, uint16_t len) {
    uint8_t tx_buf[512];
    int tx_len = tuya_pack_frame(cmd, data, len, tx_buf);
    if (g_uart_fd > 0) {
        write(g_uart_fd, tx_buf, tx_len);
    }
}

void *uart_rx_thread(void *arg) {
    uint8_t buf[256];
    tuya_parser_t parser;
    tuya_parser_init(&parser);

    while (1) {
        int n = read(g_uart_fd, buf, sizeof(buf));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (tuya_parser_process(&parser, buf[i])) {
                    // 成功解析出一帧完整数据
                    LOG_D("Tuya Frame Received: CMD=0x%02X, LEN=%d", parser.cmd, parser.data_len);
                    
                    switch (parser.cmd) {
                        case CMD_HEARTBEAT:
                            LOG_D("[MCU -> Target] Heartbeat Response Received.");
                            break;
                        case CMD_PRODUCT_INFO:
                            LOG_I("[MCU -> Target] Product Info: %.*s", parser.data_len, parser.data_buf);
                            break;
                        case CMD_DP_REPORT:
                            LOG_I("[MCU -> Target] DP Status Report Received (Length: %d).", parser.data_len);
                            // 这里你可以解析 DP 并上报给涂鸦云/AI云
                            break;
                        default:
                            LOG_I("[MCU -> Target] Other CMD Received: 0x%02X", parser.cmd);
                            break;
                    }
                }
            }
        } else {
            usleep(10000); // 10ms
        }
    }
    return NULL;
}