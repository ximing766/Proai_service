#include "../inc/utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

void bin2hex(const uint8_t *bin, int len, char *hex) {
    for (int i = 0; i < len; i++) {
        sprintf(hex + i * 2, "%02X", bin[i]);
    }
    hex[len * 2] = '\0';
}

int hex2bin(const char *hex, uint8_t *bin, int max_len) {
    int len = strlen(hex);
    if (len % 2 != 0) return -1;
    int bin_len = len / 2;
    if (bin_len > max_len) bin_len = max_len;
    
    for (int i = 0; i < bin_len; i++) {
        sscanf(hex + i * 2, "%02hhX", &bin[i]);
    }
    return bin_len;
}

int open_uart(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        return -1;
    }
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, BAUD);
    cfsetospeed(&options, BAUD);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    tcsetattr(fd, TCSANOW, &options);
    return fd;
}
