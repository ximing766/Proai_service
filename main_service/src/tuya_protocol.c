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
