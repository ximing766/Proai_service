#ifndef TUYA_PROTOCOL_H
#define TUYA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define TUYA_FRAME_HEAD     0x55AA
#define TUYA_MIN_FRAME_LEN  7

#define CMD_HEARTBEAT       0x00
#define CMD_PRODUCT_INFO    0x01
#define CMD_WORK_MODE       0x02
#define CMD_WIFI_STATE      0x03
#define CMD_RESET           0x04
#define CMD_DP_SEND         0x06
#define CMD_DP_REPORT       0x07
#define CMD_DP_QUERY        0x08
#define CMD_UPGRADE_START   0x0a
#define CMD_UPGRADE_TRANS   0x0b

uint8_t tuya_check_sum(uint8_t *data, int len);
int tuya_pack_frame(uint8_t cmd, uint8_t *data, uint16_t len, uint8_t *out_buf);

typedef enum {
    STATE_HEAD_1,
    STATE_HEAD_2,
    STATE_VERSION,
    STATE_CMD,
    STATE_LEN_1,
    STATE_LEN_2,
    STATE_DATA,
    STATE_CHECKSUM
} parse_state_t;

typedef struct {
    parse_state_t state;
    uint8_t version;
    uint8_t cmd;
    uint16_t data_len;
    uint16_t data_idx;
    uint8_t data_buf[1024];
    uint8_t checksum; // Running checksum
} tuya_parser_t;

void tuya_parser_init(tuya_parser_t *parser);
// 返回 1 表示解析出一帧完整数据，0 表示未完成
int tuya_parser_process(tuya_parser_t *parser, uint8_t byte);

#endif
