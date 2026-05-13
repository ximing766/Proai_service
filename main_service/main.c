#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "inc/log.h"
#include "inc/cloud_llm.h"
#include "inc/tuya_protocol.h"
#include "inc/queue.h"
#include "inc/audio_module.h"
#include "inc/ipc_server.h"

MsgQueue g_sys_queue;
static const char *g_ipc_bind_ip = "127.0.0.1";
static const int g_ipc_cmd_port = 19090;

// 初始化系统
void init_system(int log_to_file, LogLevel log_level);
void cleanup_system();
void run_event_loop();

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
            int parsed_level = atoi(argv[++i]);
            if (parsed_level < LOG_LEVEL_DEBUG || parsed_level > LOG_LEVEL_NONE) {
                parsed_level = LOG_LEVEL_INFO;
            }
            log_level = (LogLevel)parsed_level;
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
        // 可以在这里执行定时任务，例如：每 15 秒发一次心跳
        static int heartbeat_counter = 0;
        heartbeat_counter++;
        if (heartbeat_counter >= 10) {
            tuya_send_cmd(CMD_HEARTBEAT, NULL, 0);
            heartbeat_counter = 0;
        }
        
        // 每 15 秒发送一次文本测试请求（验证已注册 IOT 能力是否返回 tool_calls）
        static int test_counter = 0;
        test_counter++;
        if (test_counter >= 15) {
            cloud_llm_send_text("请把座椅加热打开，并把温度设置为26度，电机强度设置为2档。");
            test_counter = 0;
        }
    }
}

// 系统初始化
void init_system(int log_to_file, LogLevel log_level) {
    log_init(log_to_file);
    log_set_level(log_level); // 使用 CLI 传入的日志级别

    LOG_I("System Starting...");

    // 0. 初始化系统消息队列 (容量 16)
    if (msg_queue_init(&g_sys_queue, 16) != 0) {
        LOG_E("Failed to init message queue");
        exit(1);
    }

    // 0.5 初始化音频模块
    audio_module_init();

    // 0.6 初始化本地语音模块控制通道（JSON over TCP, 长连接）
    if (ipc_server_start(g_ipc_bind_ip, g_ipc_cmd_port) != 0) {
        LOG_W("IPC command server init failed: %s:%d", g_ipc_bind_ip, g_ipc_cmd_port);
    }

    // 1. 启动 UART 线程（它会自动处理连接和重连）
    tuya_uart_init("/dev/ttyFIQ0");

    // 2. 使用官方提供的公网测试设备凭据初始化 AI
    const char *test_device_id = "0001";
    const char *test_device_secret = "K2JJTF9SWL4NWWK28DRP7W9YAX4FSRAQ";
    
    if (cloud_llm_init(test_device_id, test_device_secret) != 0) {
        LOG_W("Failed to init AI Platform");
    }
}

// 系统清理
void cleanup_system() {
    tuya_uart_cleanup();
    ipc_server_stop();
    cloud_llm_cleanup();
    audio_module_cleanup();
    msg_queue_destroy(&g_sys_queue);
    log_close();
}
