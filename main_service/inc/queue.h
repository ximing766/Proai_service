#ifndef _SYS_QUEUE_H_
#define _SYS_QUEUE_H_

#include <stdint.h>
#include <pthread.h>

typedef enum {
    MSG_TYPE_AI_CMD,           // AI 下发的控制指令
    MSG_TYPE_TUYA_CMD,         // 涂鸦下发的控制指令
    MSG_TYPE_MCU_REPORT,       // MCU 上报的状态
    MSG_TYPE_TIMER_TICK,       // 定时器/心跳任务
    MSG_TYPE_OFFLINE_VOICE_CMD // 离线语音模块识别的控制指令
} MsgType;

typedef struct {
    MsgType type;
    uint8_t cmd;          // MCU 协议的命令字 (CMD_DP_SEND 等)
    uint8_t *data;        // 动态分配的数据内容，需要在出队后 free()
    uint16_t len;         // 数据长度
} SystemMsg;

typedef struct {
    SystemMsg *buffer;
    int capacity;
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} MsgQueue;

int msg_queue_init(MsgQueue *q, int capacity);
int msg_queue_push(MsgQueue *q, const SystemMsg *msg);
int msg_queue_pop(MsgQueue *q, SystemMsg *msg, int timeout_ms);
void msg_queue_destroy(MsgQueue *q);

// 全局系统消息队列，用于接收各模块下发的控制指令
extern MsgQueue g_sys_queue;

#endif // _SYS_QUEUE_H_
