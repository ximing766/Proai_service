#ifndef CLOUD_TUYA_H
#define CLOUD_TUYA_H

#include <stdint.h>
#include <stddef.h>

int cloud_tuya_init(void);
int cloud_tuya_report_dp(uint8_t dpid, uint8_t type, const uint8_t *data, size_t len);
void cloud_tuya_cleanup(void);

#endif // CLOUD_TUYA_H