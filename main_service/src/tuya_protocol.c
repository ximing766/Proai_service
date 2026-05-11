#include "../inc/tuya_protocol.h"
#include "../inc/log.h"
#include "../inc/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

static int g_uart_fd = -1;
static pthread_t g_uart_thread;

static void on_mcu_heartbeat(const tuya_parser_t *parser, void *user_data) {
    (void)parser;
    (void)user_data;
    LOG_D("[MCU -> Target] Heartbeat Response Received.");
}

static void on_mcu_product_info(const tuya_parser_t *parser, void *user_data) {
    (void)user_data;
    LOG_I("[MCU -> Target] Product Info: %.*s", parser->data_len, parser->data_buf);
}

// 8.3 状态上报
static void on_mcu_dp_report(const tuya_parser_t *parser, void *user_data) {
    (void)parser;
    (void)user_data;
    LOG_I("[MCU -> Target] DP Status Report Received (Length: %d).", parser->data_len);
    // 这里你可以解析 DP 并上报给涂鸦云/AI云
}

static void on_mcu_reset(const tuya_parser_t *parser, void *user_data) {
    (void)parser;
    (void)user_data;
    LOG_W("[MCU -> Target] MCU Reset Notification Received.");
}

static void on_mcu_get_version(const tuya_parser_t *parser, void *user_data) {
    (void)user_data;
    LOG_I("[MCU -> Target] MCU Version Reply: %.*s", parser->data_len, parser->data_buf);
}

static void on_mcu_cmd_22(const tuya_parser_t *parser, void *user_data) {
    (void)user_data;
    LOG_I("[MCU -> Target] CMD 0x22 Received (Length: %d).", parser->data_len);
}

static void on_mcu_cmd_26(const tuya_parser_t *parser, void *user_data) {
    (void)user_data;
    LOG_I("[MCU -> Target] CMD 0x26 Received (Length: %d).", parser->data_len);
}

static void on_mcu_default(const tuya_parser_t *parser, void *user_data) {
    (void)user_data;
    LOG_I("[MCU -> Target] Other CMD Received: 0x%02X", parser->cmd);
}

static const tuya_mcu_dispatcher_t g_mcu_dispatcher = {
    .on_heartbeat = on_mcu_heartbeat,
    .on_product_info = on_mcu_product_info,
    .on_dp_report = on_mcu_dp_report,
    .on_reset = on_mcu_reset,
    .on_get_m_version = on_mcu_get_version,
    .on_cmd_22 = on_mcu_cmd_22,
    .on_cmd_26 = on_mcu_cmd_26,
    .on_default = on_mcu_default,
};

static int configure_uart_fd(int fd) { 
    struct termios options; 
    if (tcgetattr(fd, &options) != 0) { 
        LOG_E("UART tcgetattr failed (errno: %d)", errno); 
        return -1; 
    } 

    cfmakeraw(&options); 
    cfsetispeed(&options, B115200); 
    cfsetospeed(&options, B115200); 

    options.c_cflag |= (CLOCAL | CREAD); 
    options.c_cflag &= ~CSIZE; 
    options.c_cflag |= CS8; 
    options.c_cflag &= ~PARENB; 
    options.c_cflag &= ~CSTOPB; 
    options.c_cflag &= ~CRTSCTS; 

    options.c_cc[VMIN] = 0; 
    options.c_cc[VTIME] = 1; 

    if (tcsetattr(fd, TCSANOW, &options) != 0) { 
        LOG_E("UART tcsetattr failed (errno: %d)", errno); 
        return -1; 
    } 
    tcflush(fd, TCIOFLUSH); 
    return 0; 
} 

uint8_t tuya_check_sum(uint8_t *data, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

int tuya_pack_frame(uint8_t cmd, uint8_t *data, uint16_t len, uint8_t *out_buf) {
    int idx = 0;
    out_buf[idx++] = 0x55;
    out_buf[idx++] = 0xAA;
    out_buf[idx++] = TUYA_VERSION; // Version
    out_buf[idx++] = cmd;
    out_buf[idx++] = (len >> 8) & 0xFF;
    out_buf[idx++] = len & 0xFF;
    if (len > 0 && data != NULL) {
        memcpy(&out_buf[idx], data, len);
        idx += len;
    }
    out_buf[idx] = tuya_check_sum(out_buf, idx);
    idx++;
    return idx;
}

int tuya_pack_dp_raw(uint8_t dp_id, uint8_t dp_type, const uint8_t *dp_value, uint16_t dp_value_len,
                     uint8_t *out_buf, uint16_t out_buf_size) {
    // DP format: dp_id(1) + dp_type(1) + len(2) + value(N)
    uint16_t total = (uint16_t)(4 + dp_value_len);
    if (out_buf == NULL) return -1;
    if (dp_value_len > 0 && dp_value == NULL) return -1;
    if (total > out_buf_size) return -1;

    int idx = 0;
    out_buf[idx++] = dp_id;
    out_buf[idx++] = dp_type;
    out_buf[idx++] = (uint8_t)((dp_value_len >> 8) & 0xFF);
    out_buf[idx++] = (uint8_t)(dp_value_len & 0xFF);
    if (dp_value_len > 0) {
        memcpy(&out_buf[idx], dp_value, dp_value_len);
        idx += dp_value_len;
    }
    return idx;
}

int tuya_pack_dp_bool(uint8_t dp_id, uint8_t value, uint8_t *out_buf, uint16_t out_buf_size) {
    uint8_t v = value ? 1 : 0;
    return tuya_pack_dp_raw(dp_id, DP_TYPE_BOOL, &v, 1, out_buf, out_buf_size);
}

int tuya_pack_dp_enum(uint8_t dp_id, uint8_t value, uint8_t *out_buf, uint16_t out_buf_size) {
    return tuya_pack_dp_raw(dp_id, DP_TYPE_ENUM, &value, 1, out_buf, out_buf_size);
}

int tuya_pack_dp_value(uint8_t dp_id, int32_t value, uint8_t *out_buf, uint16_t out_buf_size) {
    uint8_t buf[4];
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
    return tuya_pack_dp_raw(dp_id, DP_TYPE_VALUE, buf, 4, out_buf, out_buf_size);
}

void tuya_parser_init(tuya_parser_t *parser) {
    memset(parser, 0, sizeof(tuya_parser_t));
    parser->state = STATE_HEAD_1;
}

int tuya_parser_process(tuya_parser_t *parser, uint8_t byte) {
    switch (parser->state) {
        case STATE_HEAD_1:
            if (byte == 0x55) {
                parser->state = STATE_HEAD_2;
                parser->checksum = 0x55; // Reset and start checksum
            }
            break;
        case STATE_HEAD_2:
            if (byte == 0xAA) {
                parser->state = STATE_VERSION;
                parser->checksum += 0xAA;
            } else {
                parser->state = STATE_HEAD_1;
            }
            break;
        case STATE_VERSION:
            parser->version = byte;
            parser->state = STATE_CMD;
            parser->checksum += byte;
            break;
        case STATE_CMD:
            parser->cmd = byte;
            parser->state = STATE_LEN_1;
            parser->checksum += byte;
            break;
        case STATE_LEN_1:
            parser->data_len = byte << 8;
            parser->state = STATE_LEN_2;
            parser->checksum += byte;
            break;
        case STATE_LEN_2:
            parser->data_len |= byte;
            parser->checksum += byte;
            if (parser->data_len > 0) {
                parser->data_idx = 0;
                parser->state = STATE_DATA;
                // Protection against buffer overflow
                if (parser->data_len > sizeof(parser->data_buf)) {
                    parser->state = STATE_HEAD_1; 
                }
            } else {
                parser->state = STATE_CHECKSUM;
            }
            break;
        case STATE_DATA:
            parser->data_buf[parser->data_idx++] = byte;
            parser->checksum += byte;
            if (parser->data_idx == parser->data_len) {
                parser->state = STATE_CHECKSUM;
            }
            break;
        case STATE_CHECKSUM:
            parser->state = STATE_HEAD_1;
            if (parser->checksum == byte) {
                return 1; // Checksum matched
            } else {
                // Checksum failed, discard frame
                return 0; 
            }
    }
    return 0;
}

void tuya_dispatch_mcu_frame(const tuya_parser_t *parser, const tuya_mcu_dispatcher_t *dispatcher, void *user_data) {
    if (!parser || !dispatcher) return;

    switch (parser->cmd) {
        case CMD_HEARTBEAT:
            if (dispatcher->on_heartbeat) dispatcher->on_heartbeat(parser, user_data);
            break;
        case CMD_PRODUCT_INFO:
            if (dispatcher->on_product_info) dispatcher->on_product_info(parser, user_data);
            break;
        case CMD_DP_REPORT:
            if (dispatcher->on_dp_report) dispatcher->on_dp_report(parser, user_data);
            break;
        case CMD_RESET:
            if (dispatcher->on_reset) dispatcher->on_reset(parser, user_data);
            break;
        case CMD_GET_M_VERSION:
            if (dispatcher->on_get_m_version) dispatcher->on_get_m_version(parser, user_data);
            break;
        case CMD_MCU_FUNC_22:
            if (dispatcher->on_cmd_22) dispatcher->on_cmd_22(parser, user_data);
            break;
        case CMD_MCU_FUNC_26:
            if (dispatcher->on_cmd_26) dispatcher->on_cmd_26(parser, user_data);
            break;
        default:
            if (dispatcher->on_default) dispatcher->on_default(parser, user_data);
            break;
    }
}

static void *uart_rx_thread(void *arg) {
    const char *dev = (const char *)arg;
    uint8_t buf[256];
    tuya_parser_t parser;
    tuya_parser_init(&parser);

    LOG_I("UART background thread started for %s", dev);

    while (1) {
        if (g_uart_fd <= 0) {
            // 尝试打开串口
            g_uart_fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
            if (g_uart_fd > 0) {
                if (configure_uart_fd(g_uart_fd) != 0) {
                    close(g_uart_fd);
                    g_uart_fd = -1;
                    usleep(2000000);
                    continue;
                }
                LOG_I("UART connected to %s (fd: %d)", dev, g_uart_fd);
                
                // 连接成功后，可以主动查询一次 MCU 信息
                SystemMsg msg_info = { .type = MSG_TYPE_TIMER_TICK, .cmd = CMD_PRODUCT_INFO, .data = NULL, .len = 0 };
                msg_queue_push(&g_sys_queue, &msg_info);
            } else {
                // 没打开成功，等一下再试，不阻塞主程序
                usleep(2000000); // 2秒重试一次
                continue;
            }
        }

        // 已经连接，开始读取
        int n = read(g_uart_fd, buf, sizeof(buf));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (tuya_parser_process(&parser, buf[i])) {
                    LOG_D("Tuya Frame Received: CMD=0x%02X, LEN=%d", parser.cmd, parser.data_len);
                    tuya_dispatch_mcu_frame(&parser, &g_mcu_dispatcher, NULL);
                }
            }
        } else if (n < 0) {
            // 如果是因为非阻塞模式下暂时没数据，忽略它
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(20000); // 20ms
                continue;
            }
            // 真正的读取报错，可能是模拟器关了
            LOG_W("UART connection lost (errno: %d), closing...", errno);
            close(g_uart_fd);
            g_uart_fd = -1;
            usleep(5000000); // 5秒后进入下一次循环尝试重连
        } else {
            // 返回 0 通常表示对端关闭（在某些伪终端或管道中）
            LOG_W("UART connection closed by peer, reconnecting...");
            close(g_uart_fd);
            g_uart_fd = -1;
            usleep(2000000);
        }
    }
    return NULL;
}

int tuya_uart_init(const char *dev) {
    // 仅仅启动线程，让线程去负责连接和维护
    pthread_create(&g_uart_thread, NULL, uart_rx_thread, (void *)dev);
    return 0;
}

void tuya_uart_cleanup(void) {
    if (g_uart_fd > 0) {
        close(g_uart_fd);
        g_uart_fd = -1;
    }
    // 注意：如果是后台分离线程，这里直接退出即可
}

void tuya_send_cmd(uint8_t cmd, uint8_t *data, uint16_t len) {
    if (len > 256) {
        LOG_E("tuya_send_cmd payload too large: %d", len);
        return;
    }
    uint8_t tx_buf[512];
    int tx_len = tuya_pack_frame(cmd, data, len, tx_buf);
    
    // 增加对 fd 的检查，避免向已关闭或无效的 fd 写入
    int fd = g_uart_fd;
    if (fd > 0) {
        if (write(fd, tx_buf, tx_len) < 0) {
            LOG_W("UART write failed, MCU might be disconnected.");
        }
    }
}
