#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../inc/ota_handler.h"
#include "../inc/tuya_protocol.h"
#include "../inc/md5.h"
#include "../inc/log.h"

#define MAX_FW_SIZE (2 * 1024 * 1024) // 2MB Max
#define PACKET_SIZE 256               // Data chunk size per packet

static ota_state_t g_state = OTA_IDLE;
static uint8_t *g_fw_buf = NULL;
static uint32_t g_fw_len = 0;
static uint32_t g_current_offset = 0;
static int g_uart_fd = -1;
static ota_notify_cb_t g_notify_cb = NULL;

void ota_init(int uart_fd_in, ota_notify_cb_t notify_cb) {
    g_uart_fd = uart_fd_in;
    g_notify_cb = notify_cb;
}

int is_ota_in_progress() {
    return g_state != OTA_IDLE && g_state != OTA_DONE && g_state != OTA_ERROR;
}

static void send_to_uart(uint8_t cmd, uint8_t *data, uint16_t len) {
    uint8_t tx_buf[1024];
    int tx_len = tuya_pack_frame(cmd, data, len, tx_buf);
    if (g_uart_fd > 0) {
        write(g_uart_fd, tx_buf, tx_len);
    }
}

static void notify_master(int code, const char *payload) {
    if (g_notify_cb) {
        g_notify_cb(code, payload);
    }
}

int ota_start(const char *filepath) {
    if (is_ota_in_progress()) {
        LOG_W("OTA already in progress");
        return -1;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_E("Failed to open firmware file: %s", filepath);
        notify_master(-1, "File not found");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    g_fw_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (g_fw_len > MAX_FW_SIZE) {
        LOG_E("Firmware too large");
        fclose(f);
        notify_master(-2, "File too large");
        return -1;
    }

    if (g_fw_buf) free(g_fw_buf);
    g_fw_buf = malloc(g_fw_len);
    if (!g_fw_buf) {
        fclose(f);
        return -1;
    }
    fread(g_fw_buf, 1, g_fw_len, f);
    fclose(f);

    // Calculate MD5
    uint8_t digest[16];
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, g_fw_buf, g_fw_len);
    MD5Final(digest, &ctx);

    // Send 0x0A to MCU
    // Payload: Size (4 bytes BE) + MD5 (16 bytes)
    uint8_t payload[20];
    payload[0] = (g_fw_len >> 24) & 0xFF;
    payload[1] = (g_fw_len >> 16) & 0xFF;
    payload[2] = (g_fw_len >> 8) & 0xFF;
    payload[3] = g_fw_len & 0xFF;
    memcpy(payload + 4, digest, 16);

    send_to_uart(CMD_UPGRADE_START, payload, 20);   // MYTODO:可能需要区分版本1和版本0   LEN+STR(MD5)+VERSION
    g_state = OTA_STARTING;
    g_current_offset = 0;

    LOG_I("OTA Started. Size: %d", g_fw_len);
    notify_master(0, "OTA Started");
    return 0;
}

static void send_next_packet() {
    if (g_current_offset >= g_fw_len) {
        g_state = OTA_DONE;
        LOG_I("OTA Finished Successfully");
        notify_master(100, "OTA Success");
        return;
    }

    uint32_t remain = g_fw_len - g_current_offset;
    uint16_t chunk_len = (remain > PACKET_SIZE) ? PACKET_SIZE : remain;

    // 0x0B: [Offset(4)] [Data(N)]
    uint8_t *payload = malloc(4 + chunk_len);
    payload[0] = (g_current_offset >> 24) & 0xFF;
    payload[1] = (g_current_offset >> 16) & 0xFF;
    payload[2] = (g_current_offset >> 8) & 0xFF;
    payload[3] = g_current_offset & 0xFF;
    memcpy(payload + 4, g_fw_buf + g_current_offset, chunk_len);

    send_to_uart(CMD_UPGRADE_TRANS, payload, 4 + chunk_len);
    free(payload);
    
    // we don't increment offset here. We wait for response.
}

void ota_handle_mcu_msg(uint8_t cmd, const uint8_t *data, int len) {
    if (g_state == OTA_IDLE) return;

    if (cmd == CMD_UPGRADE_START) {
        if (len >= 1 && data[0] == 0x00) {  // MYTODO: data: 00: 256B  01: 512B  02:1024B
            LOG_I("MCU Accepted Upgrade. Sending first packet...");
            g_state = OTA_SENDING;
            g_current_offset = 0;
            send_next_packet();
        } else {
            LOG_E("MCU Rejected Upgrade");
            g_state = OTA_ERROR;
            notify_master(-3, "MCU Rejected");
        }
    } else if (cmd == CMD_UPGRADE_TRANS) {
        // MCU response to Data Packet
        // Data: 0x00 for success
        if (len >= 1 && data[0] == 0x00) {       // MYTODO: 0B返回没有data字段
            uint32_t remain = g_fw_len - g_current_offset;
            uint16_t chunk_len = (remain > PACKET_SIZE) ? PACKET_SIZE : remain;
            g_current_offset += chunk_len;
            
            // Progress report (every 10% maybe? or just packet level debug)
            LOG_D("OTA Progress: %d/%d", g_current_offset, g_fw_len);
            
            send_next_packet();
        } else {
            LOG_E("MCU Failed to receive packet");
            g_state = OTA_ERROR;
            notify_master(-4, "Packet Error");
        }
    }
}
