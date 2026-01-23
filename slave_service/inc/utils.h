#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdint.h>

#define BAUD 115200
#define UART_DEV "/dev/ttyS1"
#define IPC_PORT 5555

void bin2hex(const uint8_t *bin, int len, char *hex);
int hex2bin(const char *hex, uint8_t *bin, int max_len);
int open_uart(const char *dev);
int init_socket_server(int port);

#endif // _UTILS_H_
