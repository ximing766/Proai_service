#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include "inc/tuya_protocol.h"
#include "inc/log.h"
#include "inc/utils.h"
#include "inc/ota_handler.h"
#include "inc/cloud_llm.h"
#include "inc/cloud_tuya.h"

int uart_fd = -1;
tuya_parser_t parser;

void handle_mcu_frame(tuya_parser_t *p);
void init_system(int log_to_file);
void cleanup_system();
void run_event_loop();
void send_mcu_cmd(uint8_t cmd, const uint8_t *data, uint16_t len);

// 回调函数，用于 OTA 和 其他模块 向 MCU 发送指令
void on_ota_notify(int code, const char *msg) {
    LOG_I("OTA Notify: Code=%d Msg=%s", code, msg);
    // 这里可以根据业务将 OTA 进度同步给云端
    // cloud_tuya_report_dp(...);
}

// 发送指令给 MCU
void send_mcu_cmd(uint8_t cmd, const uint8_t *data, uint16_t len) {
    uint8_t tx_buf[1024];
    int tx_len = tuya_pack_frame(cmd, (uint8_t*)data, len, tx_buf);
    
    if (uart_fd > 0) {
        write(uart_fd, tx_buf, tx_len);
        
        char *tx_hex = malloc(tx_len * 2 + 1);
        if (tx_hex) {
            bin2hex(tx_buf, tx_len, tx_hex);
            LOG_D("UART TX: %s", tx_hex);
            free(tx_hex);
        }
    } else {
        LOG_I("Mock UART TX Cmd: 0x%02X", cmd);
    }
}

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

// 主事件循环，主要处理串口数据
void run_event_loop() {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    
    int max_fd = 0;
    
    if (uart_fd > 0) {
        FD_SET(uart_fd, &read_fds);
        max_fd = uart_fd;
    } else {
        // 如果没有串口，防止死循环占用100% CPU
        sleep(1);
        return;
    }

    // 阻塞等待
    int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    
    if (activity < 0 && errno != EINTR) {
        perror("select");
        return;
    }

    // 处理 UART 消息 (MCU -> Linux)
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

// 处理从 MCU 收到的一帧完整数据
void handle_mcu_frame(tuya_parser_t *p) {
    LOG_I("Recv MCU Frame: Cmd=0x%02X Len=%d", p->cmd, p->data_len);
    
    // Check OTA first
    if (is_ota_in_progress() && (p->cmd == CMD_UPGRADE_START || p->cmd == CMD_UPGRADE_TRANS)) {
        ota_handle_mcu_msg(p->cmd, p->data_buf, p->data_len);
        return; // 已处理，直接返回
    }
    
    // 其他通用指令处理
    switch (p->cmd) {
        case CMD_HEARTBEAT:
            // 响应心跳
            send_mcu_cmd(CMD_HEARTBEAT, NULL, 0);
            break;
        case CMD_DP_REPORT:
            // MCU 上报了 DP 状态
            if (p->data_len >= 4) {
                uint8_t dpid = p->data_buf[0];
                uint8_t type = p->data_buf[1];
                uint16_t len = (p->data_buf[2] << 8) | p->data_buf[3];
                const uint8_t *val = &p->data_buf[4];
                
                // 将 DP 状态同步给两路云端
                cloud_tuya_report_dp(dpid, type, val, len);
                
                // 将状态转换为 JSON 同步给大模型 (假设有需要)
                // char json_buf[128];
                // snprintf(json_buf, sizeof(json_buf), "{\"dpid\":%d,\"type\":%d}", dpid, type);
                // cloud_llm_send_json(json_buf);
            }
            break;
        default:
            LOG_I("Unhandled MCU Cmd: 0x%02X", p->cmd);
            break;
    }
}

// 系统初始化
void init_system(int log_to_file) {
    log_init(log_to_file);

    // 1. 初始化 UART
    uart_fd = open_uart(UART_DEV); 
    if (uart_fd < 0) {
        LOG_W("Failed to open UART %s. Running in Mock Mode.", UART_DEV);
    } else {
        LOG_I("UART opened successfully: %s", UART_DEV);
    }
    
    tuya_parser_init(&parser);
    
    // 2. 初始化 OTA (MCU)
    ota_init(uart_fd, on_ota_notify);
    
    // 3. 初始化云端大模型连接 (Agent SDK)
    if (cloud_llm_init() != 0) {
        LOG_W("Failed to init Cloud LLM");
    }
    
    // 4. 初始化涂鸦云平台直连 (Tuya Link SDK)
    if (cloud_tuya_init() != 0) {
        LOG_W("Failed to init Cloud Tuya");
    }
}

// 系统清理
void cleanup_system() {
    cloud_tuya_cleanup();
    cloud_llm_cleanup();
    
    if (uart_fd > 0) close(uart_fd);
    log_close();
}