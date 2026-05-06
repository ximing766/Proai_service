#include "../inc/tuya_protocol.h"
#include <string.h>

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
