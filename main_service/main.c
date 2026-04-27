#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "inc/log.h"
#include "inc/cloud_llm.h"

void init_system(int log_to_file);
void cleanup_system();
void run_event_loop();

// 主函数
int main(int argc, char *argv[]) {
    int log_to_file = 0;
    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        log_to_file = 1;
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
    // 模拟等待事件或指令
    sleep(5);

    // 可以在这里模拟发送心跳或者发送测试文本指令给AI平台
    static int count = 0;
    if (count++ % 6 == 0) {
        LOG_I("Mock Send Test Text to AI Platform...");
        cloud_llm_send_text("Hello, this is a test from ARM target board.");
    }
}

// 系统初始化
void init_system(int log_to_file) {
    log_init(log_to_file);

    LOG_I("System Starting...");

    // 初始化云端大模型连接 (封装了Agent SDK)
    // 注意：这里使用硬编码的 device_id, client_id, auth_token 作为演示
    const char *test_device_id = "test-device-arm71";
    const char *test_client_id = "proai-linux-client";
    const char *test_token = "Bearer test_token_xyz";
    
    if (cloud_llm_init(test_device_id, test_client_id, test_token) != 0) {
        LOG_W("Failed to init AI Platform");
    }
}

// 系统清理
void cleanup_system() {
    cloud_llm_cleanup();
    log_close();
}