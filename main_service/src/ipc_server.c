#include "../inc/ipc_server.h"
#include "../inc/log.h"
#include "../inc/queue.h"
#include "../inc/tuya_protocol.h"
#include "../inc/cJSON.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define IPC_MAX_LINE 1024

static int g_listen_fd = -1;
static pthread_t g_ipc_thread;
static volatile int g_ipc_running = 0;
static char g_bind_ip[64] = {0};
static int g_bind_port = 0;

static void send_json_response(int fd, int ok, const char *message) {
    char resp[256];
    int written = snprintf(resp, sizeof(resp),
                           "{\"ok\":%s,\"message\":\"%s\"}\n",
                           ok ? "true" : "false",
                           message ? message : "");
    if (written > 0) {
        (void)write(fd, resp, (size_t)written);
    }
}

static int enqueue_mcu_cmd(uint8_t cmd, uint8_t *data, uint16_t len, MsgType type) {
    SystemMsg msg;
    msg.type = type;
    msg.cmd = cmd;
    msg.data = data;
    msg.len = len;

    if (msg_queue_push(&g_sys_queue, &msg) != 0) {
        if (data) free(data);
        LOG_E("IPC queue push failed");
        return -1;
    }
    return 0;
}

static int handle_intent(const cJSON *root) {
    const cJSON *intent = cJSON_GetObjectItemCaseSensitive(root, "intent");
    if (!cJSON_IsString(intent) || intent->valuestring == NULL) return -1;

    const char *name = intent->valuestring;
    uint8_t *payload = (uint8_t *)malloc(5);
    if (!payload) return -1;

    if (strcmp(name, "heater_on") == 0) {
        uint8_t p[] = {0x01, 0x01, 0x00, 0x01, 0x01};
        memcpy(payload, p, sizeof(p));
    } else if (strcmp(name, "heater_off") == 0) {
        uint8_t p[] = {0x01, 0x01, 0x00, 0x01, 0x00};
        memcpy(payload, p, sizeof(p));
    } else {
        free(payload);
        return -1;
    }

    if (enqueue_mcu_cmd(CMD_DP_SEND, payload, 5, MSG_TYPE_OFFLINE_VOICE_CMD) != 0) {
        return -1;
    }
    LOG_I("IPC intent accepted: %s", name);
    return 0;
}

static int process_ipc_json(const char *line, char *errbuf, size_t errbuf_sz) {
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        snprintf(errbuf, errbuf_sz, "invalid json");
        return -1;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        cJSON_Delete(root);
        snprintf(errbuf, errbuf_sz, "missing type");
        return -1;
    }

    int ret = -1;
    if (strcmp(type->valuestring, "intent") == 0) {
        ret = handle_intent(root);
        if (ret != 0) snprintf(errbuf, errbuf_sz, "unsupported intent");
    } else if (strcmp(type->valuestring, "ping") == 0) {
        ret = 0;
    } else {
        snprintf(errbuf, errbuf_sz, "unsupported type");
        ret = -1;
    }

    cJSON_Delete(root);
    return ret;
}

static ssize_t read_line(int fd, char *buf, size_t max_len) {
    if (!buf || max_len < 2) return -1;
    size_t i = 0;
    while (i < max_len - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            if (i == 0) return 0;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

static void handle_client(int client_fd, const char *peer_ip, int peer_port) {
    char buf[IPC_MAX_LINE];
    LOG_I("IPC client connected: %s:%d", peer_ip, peer_port);
    while (g_ipc_running) {
        ssize_t n = read_line(client_fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) break;

        char err[128] = {0};
        if (process_ipc_json(buf, err, sizeof(err)) == 0) {
            send_json_response(client_fd, 1, "accepted");
        } else {
            send_json_response(client_fd, 0, err[0] ? err : "bad request");
        }
    }
    LOG_I("IPC client disconnected: %s:%d", peer_ip, peer_port);
}

static void *ipc_server_thread(void *arg) {
    (void)arg;
    while (g_ipc_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (!g_ipc_running) break;
            if (errno == EINTR) continue;
            usleep(10000);
            continue;
        }
        char peer_ip[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, peer_ip, sizeof(peer_ip));
        int peer_port = ntohs(client_addr.sin_port);
        handle_client(client_fd, peer_ip[0] ? peer_ip : "unknown", peer_port);
        close(client_fd);
    }
    return NULL;
}

int ipc_server_start(const char *bind_ip, int port) {
    if (g_ipc_running) return 0;
    if (!bind_ip || port <= 0 || port > 65535) return -1;

    memset(g_bind_ip, 0, sizeof(g_bind_ip));
    snprintf(g_bind_ip, sizeof(g_bind_ip), "%s", bind_ip);
    g_bind_port = port;

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        LOG_E("IPC socket create failed");
        return -1;
    }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_bind_port);
    if (inet_pton(AF_INET, g_bind_ip, &addr.sin_addr) != 1) {
        LOG_E("IPC invalid bind ip: %s", g_bind_ip);
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_E("IPC bind failed: %s:%d", g_bind_ip, g_bind_port);
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }
    if (listen(g_listen_fd, 4) != 0) {
        LOG_E("IPC listen failed");
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    g_ipc_running = 1;
    if (pthread_create(&g_ipc_thread, NULL, ipc_server_thread, NULL) != 0) {
        LOG_E("IPC thread create failed");
        g_ipc_running = 0;
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    LOG_I("IPC TCP server started at %s:%d", g_bind_ip, g_bind_port);
    return 0;
}

void ipc_server_stop(void) {
    if (!g_ipc_running) return;

    g_ipc_running = 0;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    pthread_join(g_ipc_thread, NULL);
    LOG_I("IPC server stopped");
}
