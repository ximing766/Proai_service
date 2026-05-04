#ifndef IPC_SERVER_H
#define IPC_SERVER_H

int ipc_server_start(const char *bind_ip, int port);
void ipc_server_stop(void);

#endif // IPC_SERVER_H
