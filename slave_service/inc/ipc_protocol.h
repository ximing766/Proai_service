#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>
#include "cJSON.h"

// IPC 协议定义
// 旧格式: [Magic(2)] [Type(1)] [Length(2)] [Payload(N)]
// 新格式: [Length(4, BigEndian)] [JSON String]

// JSON 字段定义
// {
//   "type": "heartbeat" | "send_mcu" | "evt_mcu" | "slave_status",
//   "data": { ... } // Optional, depends on type
// }

// 辅助函数声明 (实现在 ipc_protocol.c 中，或者直接在 main.c 中使用 cJSON)
// 这里我们仅保留宏定义作为参考，或者定义一些常量字符串

#define IPC_TYPE_HEARTBEAT    "heartbeat"
#define IPC_TYPE_SEND_MCU     "send_mcu"
#define IPC_TYPE_SET_DP       "set_dp"
#define IPC_TYPE_EVT_MCU      "evt_mcu"
#define IPC_TYPE_SLAVE_STATUS "slave_status"

// 对应 send_mcu 和 evt_mcu 的 data 字段
// {
//   "cmd": 123,     // integer
//   "payload": "01020304" // hex string
// }

#endif
