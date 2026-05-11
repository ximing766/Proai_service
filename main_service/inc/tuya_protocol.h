#ifndef TUYA_PROTOCOL_H
#define TUYA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define TUYA_FRAME_HEAD     0x55AA
#define TUYA_MIN_FRAME_LEN  7
#define TUYA_VERSION        0x00

// Master -> MCU
#define CMD_HEARTBEAT       0x00
#define CMD_PRODUCT_INFO    0x01
#define CMD_WORK_MODE       0x02
#define CMD_WIFI_STATE      0x03
#define CMD_DP_SEND         0x06
#define CMD_DP_QUERY        0x08
#define CMD_FUNC_22         0x22 // Reserved/Extended command from chapter 7
#define CMD_FUNC_26         0x26 // Reserved/Extended command from chapter 8
#define CMD_UPGRADE_START   0x0a
#define CMD_UPGRADE_TRANS   0x0b

// MCU -> Master
#define CMD_RESET           0x04
#define CMD_GET_M_VERSION   0x71
#define CMD_DP_REPORT       0x07
#define CMD_MCU_FUNC_22     0x22
#define CMD_MCU_FUNC_26     0x26

// DP data type
#define DP_TYPE_RAW         0x00
#define DP_TYPE_BOOL        0x01
#define DP_TYPE_VALUE       0x02
#define DP_TYPE_STRING      0x03
#define DP_TYPE_ENUM        0x04
#define DP_TYPE_BITMAP      0x05

uint8_t tuya_check_sum(uint8_t *data, int len);
int tuya_pack_frame(uint8_t cmd, uint8_t *data, uint16_t len, uint8_t *out_buf);
int tuya_pack_dp_raw(uint8_t dp_id, uint8_t dp_type, const uint8_t *dp_value, uint16_t dp_value_len,
                     uint8_t *out_buf, uint16_t out_buf_size);
int tuya_pack_dp_bool(uint8_t dp_id, uint8_t value, uint8_t *out_buf, uint16_t out_buf_size);
int tuya_pack_dp_enum(uint8_t dp_id, uint8_t value, uint8_t *out_buf, uint16_t out_buf_size);
int tuya_pack_dp_value(uint8_t dp_id, int32_t value, uint8_t *out_buf, uint16_t out_buf_size);

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

typedef struct {
    void (*on_heartbeat)(const tuya_parser_t *parser, void *user_data);
    void (*on_product_info)(const tuya_parser_t *parser, void *user_data);
    void (*on_dp_report)(const tuya_parser_t *parser, void *user_data);
    void (*on_reset)(const tuya_parser_t *parser, void *user_data);
    void (*on_get_m_version)(const tuya_parser_t *parser, void *user_data);
    void (*on_cmd_22)(const tuya_parser_t *parser, void *user_data);
    void (*on_cmd_26)(const tuya_parser_t *parser, void *user_data);
    void (*on_default)(const tuya_parser_t *parser, void *user_data);
} tuya_mcu_dispatcher_t;

// 根据 parser->cmd 分发到对应回调，未匹配则走 on_default
void tuya_dispatch_mcu_frame(const tuya_parser_t *parser, const tuya_mcu_dispatcher_t *dispatcher, void *user_data);

// 串口相关高级接口
int tuya_uart_init(const char *dev);
void tuya_uart_cleanup(void);
void tuya_send_cmd(uint8_t cmd, uint8_t *data, uint16_t len);

#endif
