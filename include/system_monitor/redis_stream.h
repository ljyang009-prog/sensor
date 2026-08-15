#ifndef SYSTEM_MONITOR_REDIS_STREAM_H
#define SYSTEM_MONITOR_REDIS_STREAM_H

#include "system_metric.h"
#include <hiredis.h>
#include <stddef.h>
#include <stdint.h>

#define REDIS_STREAM_MAXLEN_DEFAULT 100000
#define REDIS_STREAM_ID_LEN 32

/*
 * Redis Stream 客户端：采集端把指标 XADD 进 Stream，DB 端用消费组
 * XREADGROUP 批量读取后写 MySQL，再 XACK 确认。Stream 作为采集与
 * 落库之间的消息队列，起到削峰、解耦与持久缓冲的作用。
 */
typedef struct {
    redisContext *connection;
    char stream[64];
    char group[64];
    char consumer[64];
    unsigned long long maxlen;
} redis_stream_t;

/* 消费到的一条消息及其 Stream 条目 ID */
typedef struct {
    system_metric_t message;
    char id[REDIS_STREAM_ID_LEN];
} redis_stream_entry_t;

/* 连接并确保 Stream / 消费者组存在，失败返回 -1 */
int redis_stream_open(redis_stream_t *rs);

/* 断开并释放资源 */
void redis_stream_close(redis_stream_t *rs);

/* 采集端：追加一条指标到 Stream（带 MAXLEN 限制），失败返回 -1 */
int redis_stream_produce(redis_stream_t *rs, const system_metric_t *message);

/*
 * 消费端：批量读取最多 cap 条新消息，最多阻塞 timeout_ms 毫秒。
 * timeout_ms 为 0 表示非阻塞。返回读取条数；连接错误返回 -1。
 */
int redis_stream_consume(redis_stream_t *rs, redis_stream_entry_t *entries,
                         size_t cap, size_t *count, int64_t timeout_ms);

/* 消费端：确认一批消息已处理（写库成功或已落盘兜底后），失败返回 -1 */
int redis_stream_ack(redis_stream_t *rs, const redis_stream_entry_t *entries,
                     size_t count);

#endif
