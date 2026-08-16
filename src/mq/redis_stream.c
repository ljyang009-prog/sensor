#define _DEFAULT_SOURCE

#include "system_monitor/redis_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static const char *env_or_default(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}

/* 把 Redis 字符串安全复制到固定长度缓冲区，越界截断 */
static void copy_field(char *dst, size_t dst_len, const redisReply *reply) {
    size_t n = reply->len < dst_len - 1 ? reply->len : dst_len - 1;
    memcpy(dst, reply->str, n);
    dst[n] = '\0';
}

int redis_stream_open(redis_stream_t *rs) {
    const char *host, *stream, *group;
    struct timeval timeout = {2, 0};
    int port;
    redisReply *reply;

    if (!rs) return -1;
    memset(rs, 0, sizeof(*rs));

    host = env_or_default("REDIS_HOST", "127.0.0.1");
    port = (int)strtol(env_or_default("REDIS_PORT", "6379"), NULL, 10);
    stream = env_or_default("REDIS_STREAM", "system_metrics");
    group = env_or_default("REDIS_GROUP", "writers");
    rs->maxlen = strtoull(env_or_default("REDIS_MAXLEN", "100000"), NULL, 10);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "invalid REDIS_PORT\n");
        return -1;
    }
    if (rs->maxlen == 0) rs->maxlen = REDIS_STREAM_MAXLEN_DEFAULT;

    snprintf(rs->stream, sizeof(rs->stream), "%s", stream);
    snprintf(rs->group, sizeof(rs->group), "%s", group);
    snprintf(rs->consumer, sizeof(rs->consumer), "writer-%ld", (long)getpid());

    rs->connection = redisConnectWithTimeout(host, port, timeout);
    if (!rs->connection || rs->connection->err) {
        fprintf(stderr, "redis connect failed: %s\n",
                rs->connection ? rs->connection->errstr : "out of memory");
        redis_stream_close(rs);
        return -1;
    }

    /* 消费组不存在则创建；已存在(BUSYGROUP)视为正常 */
    reply = redisCommand(rs->connection, "XGROUP CREATE %s %s $ MKSTREAM",
                         rs->stream, rs->group);
    if (!reply) {
        fprintf(stderr, "redis XGROUP CREATE failed: %s\n",
                rs->connection->errstr);
        redis_stream_close(rs);
        return -1;
    }
    if (reply->type == REDIS_REPLY_ERROR &&
        strncmp(reply->str, "BUSYGROUP", 9) != 0) {
        fprintf(stderr, "redis XGROUP CREATE failed: %s\n", reply->str);
        freeReplyObject(reply);
        redis_stream_close(rs);
        return -1;
    }
    freeReplyObject(reply);

    printf("connected to Redis %s:%d stream=%s group=%s consumer=%s\n",
           host, port, rs->stream, rs->group, rs->consumer);
    return 0;
}

void redis_stream_close(redis_stream_t *rs) {
    if (!rs) return;
    if (rs->connection) redisFree(rs->connection);
    memset(rs, 0, sizeof(*rs));
}

int redis_stream_produce(redis_stream_t *rs, const system_metric_t *message) {
    char event_time[32], value[40], sequence[32];
    redisReply *reply;
    int rc = -1;

    if (!rs || !rs->connection || !message) return -1;

    snprintf(event_time, sizeof(event_time), "%lld",
             (long long)message->event_time_ms);
    snprintf(value, sizeof(value), "%.6f", message->value);
    snprintf(sequence, sizeof(sequence), "%lld", (long long)message->sequence);

    reply = redisCommand(rs->connection,
                         "XADD %s MAXLEN ~ %llu * "
                         "message_id %s device_id %s metric_type %s "
                         "event_time_ms %s value %s unit %s sequence %s "
                         "schema_version %s",
                         rs->stream, rs->maxlen, message->message_id,
                         message->device_id, message->metric_type, event_time,
                         value, message->unit, sequence, message->schema_version);
    if (!reply) {
        fprintf(stderr, "redis XADD failed: %s\n", rs->connection->errstr);
        return -1;
    }
    if (reply->type == REDIS_REPLY_ERROR)
        fprintf(stderr, "redis XADD failed: %s\n", reply->str);
    else
        rc = 0;
    freeReplyObject(reply);
    return rc;
}

/* 解析单条 Stream 条目的字段，填充 system_metric_t */
static int parse_fields(const redisReply *fields, system_metric_t *message) {
    size_t i;

    if (fields->type != REDIS_REPLY_ARRAY) return -1;
    memset(message, 0, sizeof(*message));
    for (i = 0; i + 1 < fields->elements; i += 2) {
        const redisReply *name = fields->element[i];
        const redisReply *value = fields->element[i + 1];
        if (name->type != REDIS_REPLY_STRING ||
            value->type != REDIS_REPLY_STRING)
            continue;
        if (strcmp(name->str, "message_id") == 0)
            copy_field(message->message_id, sizeof(message->message_id), value);
        else if (strcmp(name->str, "device_id") == 0)
            copy_field(message->device_id, sizeof(message->device_id), value);
        else if (strcmp(name->str, "metric_type") == 0)
            copy_field(message->metric_type, sizeof(message->metric_type), value);
        else if (strcmp(name->str, "event_time_ms") == 0)
            message->event_time_ms = (int64_t)strtoll(value->str, NULL, 10);
        else if (strcmp(name->str, "value") == 0)
            message->value = strtod(value->str, NULL);
        else if (strcmp(name->str, "unit") == 0)
            copy_field(message->unit, sizeof(message->unit), value);
        else if (strcmp(name->str, "sequence") == 0)
            message->sequence = (int64_t)strtoll(value->str, NULL, 10);
        else if (strcmp(name->str, "schema_version") == 0)
            copy_field(message->schema_version, sizeof(message->schema_version),
                       value);
    }
    return 0;
}

int redis_stream_consume(redis_stream_t *rs, redis_stream_entry_t *entries,
                         size_t cap, size_t *count, int64_t timeout_ms) {
    redisReply *reply;
    size_t i;

    if (!rs || !rs->connection || !entries || !count || cap == 0) return -1;
    *count = 0;

    /* BLOCK 0 在 Redis 中表示“永久阻塞”，不是“不阻塞”；timeout_ms<=0 时应
     * 省略 BLOCK，让 XREADGROUP 立即返回，避免退出排空时永久挂起。 */
    if (timeout_ms > 0) {
        reply = redisCommand(rs->connection,
                             "XREADGROUP GROUP %s %s COUNT %llu BLOCK %lld "
                             "STREAMS %s >",
                             rs->group, rs->consumer, (unsigned long long)cap,
                             (long long)timeout_ms, rs->stream);
    } else {
        reply = redisCommand(rs->connection,
                             "XREADGROUP GROUP %s %s COUNT %llu STREAMS %s >",
                             rs->group, rs->consumer, (unsigned long long)cap,
                             rs->stream);
    }
    if (!reply) {
        fprintf(stderr, "redis XREADGROUP failed: %s\n",
                rs->connection->errstr);
        return -1;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        fprintf(stderr, "redis XREADGROUP failed: %s\n", reply->str);
        freeReplyObject(reply);
        return -1;
    }
    if (reply->type == REDIS_REPLY_NIL || reply->type != REDIS_REPLY_ARRAY ||
        reply->elements == 0) {
        freeReplyObject(reply);
        return 0; /* 超时无消息 */
    }

    {
        redisReply *stream = reply->element[0];
        if (stream->type == REDIS_REPLY_ARRAY && stream->elements >= 2) {
            redisReply *items = stream->element[1];
            if (items->type == REDIS_REPLY_ARRAY) {
                for (i = 0; i < items->elements && *count < cap; ++i) {
                    redisReply *entry = items->element[i];
                    if (entry->type != REDIS_REPLY_ARRAY || entry->elements < 2)
                        continue;
                    redisReply *id = entry->element[0];
                    redisReply *fields = entry->element[1];
                    if (id->type != REDIS_REPLY_STRING) continue;
                    copy_field(entries[*count].id, sizeof(entries[*count].id), id);
                    if (parse_fields(fields, &entries[*count].message) == 0)
                        (*count)++;
                }
            }
        }
    }
    freeReplyObject(reply);
    return 0;
}

int redis_stream_ack(redis_stream_t *rs, const redis_stream_entry_t *entries,
                     size_t count) {
    const char **argv;
    redisReply *reply;
    size_t i;
    int rc = -1;

    if (!rs || !rs->connection) return -1;
    if (count == 0) return 0;

    argv = malloc((3 + count) * sizeof(char *));
    if (!argv) return -1;
    argv[0] = "XACK";
    argv[1] = rs->stream;
    argv[2] = rs->group;
    for (i = 0; i < count; ++i)
        argv[3 + i] = entries[i].id;

    reply = redisCommandArgv(rs->connection, (int)(3 + count), argv, NULL);
    free(argv);
    if (!reply) {
        fprintf(stderr, "redis XACK failed: %s\n", rs->connection->errstr);
        return -1;
    }
    if (reply->type == REDIS_REPLY_INTEGER)
        rc = 0;
    else
        fprintf(stderr, "redis XACK failed: %s\n", reply->str);
    freeReplyObject(reply);
    return rc;
}
