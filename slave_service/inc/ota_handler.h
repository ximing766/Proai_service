#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdint.h>

typedef enum {
    OTA_IDLE,
    OTA_STARTING,   // Sent 0x0A, waiting for ACK
    OTA_SENDING,    // Sending 0x0B packets
    OTA_DONE,
    OTA_ERROR
} ota_state_t;

void ota_init(int uart_fd_in, void (*send_cb)(const char *type, int cmd, const uint8_t *payload, int len));
int ota_start(const char *filepath);
void ota_handle_mcu_msg(uint8_t cmd, const uint8_t *data, int len);
int is_ota_in_progress();

#endif
