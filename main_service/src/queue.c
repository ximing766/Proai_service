#include "../inc/queue.h"
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

int msg_queue_init(MsgQueue *q, int capacity) {
    if (!q || capacity <= 0) return -1;
    q->buffer = (SystemMsg *)malloc(sizeof(SystemMsg) * capacity);
    if (!q->buffer) return -1;
    
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return 0;
}

int msg_queue_push(MsgQueue *q, const SystemMsg *msg) {
    if (!q || !msg) return -1;
    
    pthread_mutex_lock(&q->lock);
    while (q->count == q->capacity) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    
    q->buffer[q->tail] = *msg;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

int msg_queue_pop(MsgQueue *q, SystemMsg *msg, int timeout_ms) {
    if (!q || !msg) return -1;

    pthread_mutex_lock(&q->lock);
    
    if (timeout_ms == 0) {
        if (q->count == 0) {
            pthread_mutex_unlock(&q->lock);
            return -1;
        }
    } else {
        struct timespec ts;
        struct timeval now;
        gettimeofday(&now, NULL);
        
        long long abs_msec = now.tv_sec * 1000LL + now.tv_usec / 1000 + timeout_ms;
        ts.tv_sec = abs_msec / 1000;
        ts.tv_nsec = (abs_msec % 1000) * 1000000;

        while (q->count == 0) {
            if (timeout_ms < 0) {
                pthread_cond_wait(&q->not_empty, &q->lock);
            } else {
                int ret = pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
                if (ret != 0) {
                    pthread_mutex_unlock(&q->lock);
                    return -1;
                }
            }
        }
    }
    
    *msg = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

void msg_queue_destroy(MsgQueue *q) {
    if (!q) return;
    free(q->buffer);
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}
