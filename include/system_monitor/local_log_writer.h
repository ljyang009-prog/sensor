#ifndef SYSTEM_MONITOR_LOCAL_LOG_WRITER_H
#define SYSTEM_MONITOR_LOCAL_LOG_WRITER_H

#include "system_metric.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>

/*
 * 本地日志兜底写入器：当环形队列满或数据库写入失败时，
 * 把消息追加写入本地文件，避免数据直接丢失。
 * 采集线程与 DB 线程可能并发写入，内部用互斥锁串行化。
 */
typedef struct {
    FILE *file;
    pthread_mutex_t mutex;
} local_log_writer_t;

/* 以追加模式打开 path，失败返回 -1 */
int local_log_open(local_log_writer_t *writer, const char *path);

/* 追加写入单条消息，失败返回 -1 */
int local_log_write(local_log_writer_t *writer, const system_metric_t *message);

/* 追加写入一批消息，失败返回 -1 */
int local_log_write_batch(local_log_writer_t *writer,
                          const system_metric_t *messages, size_t count);

void local_log_close(local_log_writer_t *writer);

#endif
