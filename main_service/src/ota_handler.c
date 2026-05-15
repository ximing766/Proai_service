#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../inc/ota_handler.h"
#include "../inc/tuya_protocol.h"
#include "../inc/log.h"

#define MAX_FW_SIZE (2 * 1024 * 1024) // 2MB Max
static uint16_t g_packet_size = 256;  // Will be updated by MCU response

static ota_state_t g_state = OTA_IDLE;
static uint8_t *g_fw_buf = NULL;
static uint32_t g_fw_len = 0;
static uint32_t g_current_offset = 0;

int is_ota_in_progress(void) {
    return g_state == OTA_STARTING || g_state == OTA_SENDING;
}

int ota_start(const char *filepath) {
    if (is_ota_in_progress()) {
        LOG_W("OTA already in progress");
        return -1;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_E("Failed to open firmware file: %s", filepath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    g_fw_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (g_fw_len > MAX_FW_SIZE) {
        LOG_E("Firmware too large");
        fclose(f);
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

    // Protocol V0 Upgrade Start (0x0A) Payload:
    // Only 4 Bytes: Firmware Package Size (unsigned int, Big Endian)
    uint8_t payload[4];
    payload[0] = (g_fw_len >> 24) & 0xFF;
    payload[1] = (g_fw_len >> 16) & 0xFF;
    payload[2] = (g_fw_len >> 8) & 0xFF;
    payload[3] = g_fw_len & 0xFF;

    tuya_send_cmd(CMD_UPGRADE_START, payload, 4);
    g_state = OTA_STARTING;
    g_current_offset = 0;

    LOG_I("OTA Started (V0). Size: %d", g_fw_len);
    return 0;
}

static void send_next_packet() {
    if (g_current_offset >= g_fw_len) {
        g_state = OTA_DONE;
        LOG_I("OTA Finished Successfully");
        return;
    }

    uint32_t remain = g_fw_len - g_current_offset;
    uint16_t chunk_len = (remain > g_packet_size) ? g_packet_size : remain;

    // 0x0B: [Offset(4)] [Data(N)]
    uint8_t *payload = malloc(4 + chunk_len);
    if (!payload) return;
    payload[0] = (g_current_offset >> 24) & 0xFF;
    payload[1] = (g_current_offset >> 16) & 0xFF;
    payload[2] = (g_current_offset >> 8) & 0xFF;
    payload[3] = g_current_offset & 0xFF;
    memcpy(payload + 4, g_fw_buf + g_current_offset, chunk_len);

    tuya_send_cmd(CMD_UPGRADE_TRANS, payload, 4 + chunk_len);
    free(payload);
    
    // Do not increment offset here. Wait for MCU 0x0B response.
}

void ota_handle_mcu_msg(uint8_t cmd, const uint8_t *data, int len) {
    if (g_state == OTA_IDLE) return;

    if (cmd == CMD_UPGRADE_START) {
        if (len >= 1) {  
            // MCU replies packet size support: 00: 256B, 01: 512B, 02: 1024B
            if (data[0] == 0x00) g_packet_size = 256;
            else if (data[0] == 0x01) g_packet_size = 512;
            else if (data[0] == 0x02) g_packet_size = 1024;
            else g_packet_size = 256; // fallback
            
            LOG_I("MCU Accepted Upgrade. Chunk size: %d", g_packet_size);
            g_state = OTA_SENDING;
            g_current_offset = 0;
            send_next_packet();
        } else {
            LOG_E("MCU Rejected Upgrade (Invalid Start Response)");
            g_state = OTA_ERROR;
        }
    } else if (cmd == CMD_UPGRADE_TRANS) {
        // V0 Protocol: MCU responds with CMD_UPGRADE_TRANS (0x0B) without payload on success
        if (len == 0 || (len >= 1 && data[0] == 0x00)) { 
            uint32_t remain = g_fw_len - g_current_offset;
            uint16_t chunk_len = (remain > g_packet_size) ? g_packet_size : remain;
            g_current_offset += chunk_len;
            
            int progress = (g_current_offset * 100) / g_fw_len;
            LOG_D("OTA Progress: %d%% (%d/%d)", progress, g_current_offset, g_fw_len);
            
            send_next_packet();
        } else {
            LOG_E("MCU Failed to write packet");
            g_state = OTA_ERROR;
        }
    }
}
