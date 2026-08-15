#define _DEFAULT_SOURCE

#include "system_monitor/cpu_monitor.h"
#include "system_monitor/local_log_writer.h"
#include "system_monitor/memory_monitor.h"
#include "system_monitor/mysql_writer.h"
#include "system_monitor/redis_stream.h"
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define SAMPLE_INTERVAL_US 500000     /* 采集周期 500ms */
#define RETRY_INTERVAL_US 2000000     /* 数据库/Redis 重连间隔 2s */
#define BATCH_CAPACITY 1024            /* 单批写入条数上限 */
#define FLUSH_INTERVAL_MS 1000         /* Redis 消费阻塞时长（时间阈值） */
#define REDIS_RETRY_INTERVAL_MS 5000   /* 采集端 Redis 重连间隔 */
#define SPILL_LOG_PATH "system_metrics.spill.log"

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t collector_done = 0;
static local_log_writer_t spill_log;

static void stop_monitor(int signal_number) {
    (void)signal_number;
    running = 0;
}

static int64_t now_ms(void) {
    struct timeval value;
    gettimeofday(&value, NULL);
    return (int64_t)value.tv_sec * 1000 + value.tv_usec / 1000;
}

static void log_message(const system_metric_t *message) {
    printf("type=%s device=%s value=%.2f %s time=%" PRId64 "\n",
           message->metric_type, message->device_id, message->value,
           message->unit, message->event_time_ms);
}

/* 采集端：写 Redis Stream，失败则降级到本地落盘并标记 Redis 不可用 */
static void emit_metric(redis_stream_t *redis, int *redis_ok,
                        const system_metric_t *message) {
    if (*redis_ok && redis_stream_produce(redis, message) == 0) {
        log_message(message);
        return;
    }
    if (*redis_ok) {
        fprintf(stderr, "redis produce failed; falling back to local log\n");
        *redis_ok = 0;
        redis_stream_close(redis);
    }
    if (local_log_write(&spill_log, message) != 0)
        fprintf(stderr, "spill write failed\n");
}

static void *collector_thread_main(void *arg) {
    cpu_monitor_t cpu;
    redis_stream_t redis;
    system_metric_t messages[CPU_MONITOR_MAX_CORES + 1];
    size_t count, i;
    uint64_t memory_sequence = 1;
    int64_t last_redis_retry = 0;
    int redis_ok = 0;
    (void)arg;

    cpu_monitor_init(&cpu);
    memset(&redis, 0, sizeof(redis));
    while (running && redis_stream_open(&redis) != 0) {
        fprintf(stderr, "redis connect failed; retrying in 2s\n");
        usleep(RETRY_INTERVAL_US);
    }
    redis_ok = running;

    while (running) {
        int64_t timestamp = now_ms();
        int result = cpu_monitor_sample(&cpu, messages,
                                        CPU_MONITOR_MAX_CORES + 1, &count,
                                        timestamp);
        if (result == 0) {
            for (i = 0; i < count; ++i)
                emit_metric(&redis, &redis_ok, &messages[i]);
        } else if (result < 0) {
            fprintf(stderr, "failed to read /proc/stat\n");
        }

        system_metric_t memory;
        if (memory_monitor_sample(&memory, timestamp, memory_sequence++) == 0)
            emit_metric(&redis, &redis_ok, &memory);
        else
            fprintf(stderr, "failed to read /proc/meminfo\n");

        /* Redis 断连时周期性重连，避免每次采样都阻塞 */
        if (!redis_ok && now_ms() - last_redis_retry >= REDIS_RETRY_INTERVAL_MS) {
            last_redis_retry = now_ms();
            if (redis_stream_open(&redis) == 0)
                redis_ok = 1;
        }
        usleep(SAMPLE_INTERVAL_US);
    }
    redis_stream_close(&redis);
    collector_done = 1;
    printf("collector thread stopped\n");
    return NULL;
}

static void *db_thread_main(void *arg) {
    mysql_writer_t writer;
    redis_stream_t redis;
    redis_stream_entry_t entries[BATCH_CAPACITY];
    system_metric_t batch[BATCH_CAPACITY];
    size_t count, i;
    int mysql_ok = 0, redis_ok = 0;
    (void)arg;

    memset(&writer, 0, sizeof(writer));
    memset(&redis, 0, sizeof(redis));

    while (running && redis_stream_open(&redis) != 0) {
        fprintf(stderr, "redis connect failed; retrying in 2s\n");
        usleep(RETRY_INTERVAL_US);
    }
    redis_ok = running;

    while (running && mysql_writer_open(&writer) != 0) {
        fprintf(stderr, "database connect failed; retrying in 2s\n");
        usleep(RETRY_INTERVAL_US);
    }
    mysql_ok = running;

    /* 运行期间持续消费；采集线程停止后再排空剩余消息 */
    while (running || !collector_done) {
        /* MySQL 不可用：暂停消费，让消息堆积在 Redis（受 MAXLEN 限制） */
        if (!mysql_ok) {
            if (mysql_writer_open(&writer) == 0)
                mysql_ok = 1;
            else {
                fprintf(stderr, "database reconnect failed; retrying in 2s\n");
                usleep(RETRY_INTERVAL_US);
            }
            continue;
        }
        if (!redis_ok) {
            if (redis_stream_open(&redis) == 0)
                redis_ok = 1;
            else {
                usleep(RETRY_INTERVAL_US);
                continue;
            }
        }

        count = 0;
        if (redis_stream_consume(&redis, entries, BATCH_CAPACITY, &count,
                                 FLUSH_INTERVAL_MS) != 0) {
            redis_ok = 0;
            redis_stream_close(&redis);
            continue;
        }
        if (count == 0)
            continue; /* 超时无消息 */

        for (i = 0; i < count; ++i)
            batch[i] = entries[i].message;

        if (mysql_writer_write_batch(&writer, batch, count) == 0) {
            printf("stored %zu metrics\n", count);
        } else {
            fprintf(stderr,
                    "database write failed; spilling %zu metrics to %s\n",
                    count, SPILL_LOG_PATH);
            local_log_write_batch(&spill_log, batch, count);
            mysql_writer_close(&writer);
            mysql_ok = 0;
        }
        redis_stream_ack(&redis, entries, count);
    }

    /* 采集线程已停止：非阻塞排空 Redis 中的剩余消息 */
    for (;;) {
        if (!redis_ok)
            break;
        count = 0;
        if (redis_stream_consume(&redis, entries, BATCH_CAPACITY, &count, 0) != 0)
            break;
        if (count == 0)
            break;
        for (i = 0; i < count; ++i)
            batch[i] = entries[i].message;
        if (mysql_ok && mysql_writer_write_batch(&writer, batch, count) == 0)
            printf("stored %zu metrics (drain)\n", count);
        else
            local_log_write_batch(&spill_log, batch, count);
        redis_stream_ack(&redis, entries, count);
    }

    if (mysql_ok)
        mysql_writer_close(&writer);
    redis_stream_close(&redis);
    printf("database writer thread stopped\n");
    return NULL;
}

int main(void) {
    pthread_t collector_thread, db_thread;

    if (local_log_open(&spill_log, SPILL_LOG_PATH) != 0) {
        fprintf(stderr, "open spill log %s failed\n", SPILL_LOG_PATH);
        return 1;
    }

    signal(SIGINT, stop_monitor);
    signal(SIGTERM, stop_monitor);

    if (pthread_create(&collector_thread, NULL, collector_thread_main, NULL) != 0) {
        fprintf(stderr, "create collector thread failed\n");
        local_log_close(&spill_log);
        return 1;
    }
    if (pthread_create(&db_thread, NULL, db_thread_main, NULL) != 0) {
        fprintf(stderr, "create database writer thread failed\n");
        running = 0;
        pthread_join(collector_thread, NULL);
        local_log_close(&spill_log);
        return 1;
    }

    printf("system monitor started (collector -> Redis Stream -> database writer)\n");

    /* 主线程仅等待退出信号 */
    while (running)
        usleep(100000);

    /* 先等采集线程停止生产，DB 线程随后排空 Redis 剩余消息 */
    pthread_join(collector_thread, NULL);
    pthread_join(db_thread, NULL);

    local_log_close(&spill_log);
    printf("system monitor stopped\n");
    return 0;
}
