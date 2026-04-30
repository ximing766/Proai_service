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
#include "inc/queue.h"
#include "inc/audio_module.h"

static int g_uart_fd = -1;
static pthread_t g_uart_thread;
MsgQueue g_sys_queue;

// 初始化系统
void init_system(int log_to_file, LogLevel log_level);
void cleanup_system();
void run_event_loop();
int init_uart(const char *dev);
void *uart_rx_thread(void *arg);
void tuya_send_cmd(uint8_t cmd, uint8_t *data, uint16_t len);

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -s              Output log to stdout only (default: output to file)\n");
    printf("  -v <level>      Set log level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE (default: 1)\n");
    printf("  -h              Show this help message\n");
}

// 主函数
int main(int argc, char *argv[]) {
    int log_to_file = 1; // 默认开启文件日志
    LogLevel log_level = LOG_LEVEL_INFO; // 默认 INFO

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            log_to_file = 0;
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            log_level = (LogLevel)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    init_system(log_to_file, log_level);

    LOG_I("Entering Main Event Loop (Log Level: %d)...", log_level);
    while (1) {
        run_event_loop();
    }
    
    cleanup_system();
    return 0;
}

// 主事件循环
void run_event_loop() {
    SystemMsg msg;
    // 阻塞等待队列中的消息，超时时间为 1000ms (1秒)
    if (msg_queue_pop(&g_sys_queue, &msg, 1000) == 0) {
        // 从队列中成功获取到消息
        switch (msg.type) {
            case MSG_TYPE_AI_CMD:
            case MSG_TYPE_TUYA_CMD:
            case MSG_TYPE_TIMER_TICK:
            case MSG_TYPE_OFFLINE_VOICE_CMD:
                // 将指令下发给 MCU (唯一的串口写入点)
                tuya_send_cmd(msg.cmd, msg.data, msg.len);
                break;
            case MSG_TYPE_MCU_REPORT:
                // MCU 状态上报逻辑（可在此统一处理转发云端）
                break;
            default:
                break;
        }
        
        // 注意：数据处理完后，必须释放动态分配的内存
        if (msg.data != NULL) {
            free(msg.data);
            msg.data = NULL;
        }
    } else {
        // 队列超时 (1秒没有收到任何控制消息)
        // 可以在这里执行定时任务，例如：每 5 秒发一次心跳
        static int heartbeat_counter = 0;
        heartbeat_counter++;
        if (heartbeat_counter >= 5) {
            tuya_send_cmd(CMD_HEARTBEAT, NULL, 0);
            heartbeat_counter = 0;
        }
        
        // 每 60 秒发送一次文本测试请求
        static int test_counter = 0;
        test_counter++;
        if (test_counter >= 60) {
            LOG_I("Testing AI Platform: Sending Text Message...");
            cloud_llm_send_text("你好，请介绍一下你自己。");
            test_counter = 0;
        }
    }
}

// 系统初始化
void init_system(int log_to_file, LogLevel log_level) {
    log_init(log_to_file);
    log_set_level(log_level); // 使用 CLI 传入的日志级别

    LOG_I("System Starting...");

    // 0. 初始化系统消息队列 (容量 64)
    if (msg_queue_init(&g_sys_queue, 64) != 0) {
        LOG_E("Failed to init message queue");
        exit(1);
    }

    // 0.5 初始化音频模块
    audio_module_init();

    // 1. 初始化 UART 连接兔子板模拟器
    if (init_uart("/tmp/ttyModule") != 0) {
        LOG_W("UART init failed. Running without MCU connection.");
    } else {
        // 刚启动时，通过推入队列来发送查询指令，测试队列功能
        SystemMsg msg_info = { .type = MSG_TYPE_TIMER_TICK, .cmd = CMD_PRODUCT_INFO, .data = NULL, .len = 0 };
        msg_queue_push(&g_sys_queue, &msg_info);

        SystemMsg msg_query = { .type = MSG_TYPE_TIMER_TICK, .cmd = CMD_DP_QUERY, .data = NULL, .len = 0 };
        msg_queue_push(&g_sys_queue, &msg_query);
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
    audio_module_cleanup();
    msg_queue_destroy(&g_sys_queue);
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