#define _DEFAULT_SOURCE

#include "sensor_pipeline/cpu_monitor.h"
#include "sensor_pipeline/memory_monitor.h"
#include "sensor_pipeline/mysql_writer.h"
#include <inttypes.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#define SAMPLE_INTERVAL_US 500000
#define RETRY_INTERVAL_US 2000000
#define BATCH_CAPACITY 1024
#define FLUSH_INTERVAL_MS 1000

static volatile sig_atomic_t running = 1;
static void stop_monitor(int signal_number) { (void)signal_number; running = 0; }

static int64_t now_ms(void) {
    struct timeval value;
    gettimeofday(&value, NULL);
    return (int64_t)value.tv_sec * 1000 + value.tv_usec / 1000;
}

static int flush_batch(mysql_writer_t *writer, sensor_message_t *batch,
                       size_t *batch_count, int64_t *last_flush_ms) {
    if (*batch_count == 0) return 0;
    if (mysql_writer_write_batch(writer, batch, *batch_count) == 0) {
        printf("stored %zu metrics\n", *batch_count);
        *batch_count = 0;
        *last_flush_ms = now_ms();
        return 0;
    }
    fprintf(stderr, "database write failed; retaining %zu metrics for retry\n",
            *batch_count);
    mysql_writer_close(writer);
    while (running && mysql_writer_open(writer) != 0) {
        fprintf(stderr, "database reconnect failed; retrying in 2 seconds\n");
        usleep(RETRY_INTERVAL_US);
    }
    return -1;
}

int main(void) {
    cpu_monitor_t cpu;
    mysql_writer_t writer;
    sensor_message_t messages[CPU_MONITOR_MAX_CORES + 1];
    sensor_message_t batch[BATCH_CAPACITY];
    size_t batch_count = 0;
    size_t count, i;
    uint64_t sequence = 1;
    int64_t last_flush_ms;

    if (mysql_writer_open(&writer) != 0) return 1;
    cpu_monitor_init(&cpu);
    signal(SIGINT, stop_monitor);
    signal(SIGTERM, stop_monitor);
    last_flush_ms = now_ms();
    printf("system monitor started, sampling every 500ms\n");

    while (running) {
        /* Reserve enough space for total CPU, all cores and memory. */
        if (batch_count > BATCH_CAPACITY - (CPU_MONITOR_MAX_CORES + 2)) {
            if (flush_batch(&writer, batch, &batch_count, &last_flush_ms) != 0)
                continue;
        }
        int64_t timestamp = now_ms();
        int result = cpu_monitor_sample(&cpu, messages,
                                        CPU_MONITOR_MAX_CORES + 1,
                                        &count, timestamp);
        if (result == 0) {
            for (i = 0; i < count; ++i) {
                printf("type=%s device=%s value=%.2f %s time=%" PRId64 "\n",
                       messages[i].sensor_type, messages[i].device_id,
                       messages[i].value, messages[i].unit,
                       messages[i].event_time_ms);
                if (batch_count < BATCH_CAPACITY) {
                    batch[batch_count++] = messages[i];
                } else {
                    fprintf(stderr, "metric batch is full; CPU metric not buffered\n");
                }
            }
        } else if (result < 0) {
            fprintf(stderr, "failed to read /proc/stat\n");
        }
        sensor_message_t memory;
        if (memory_monitor_sample(&memory, timestamp, sequence++) == 0) {
            printf("type=%s device=%s value=%.2f %s time=%" PRId64 "\n",
                   memory.sensor_type, memory.device_id, memory.value,
                   memory.unit, memory.event_time_ms);
            if (batch_count < BATCH_CAPACITY) {
                batch[batch_count++] = memory;
            } else {
                fprintf(stderr, "metric batch is full; memory metric not buffered\n");
            }
        } else {
            fprintf(stderr, "failed to read /proc/meminfo\n");
        }

        if (batch_count == BATCH_CAPACITY ||
            (batch_count > 0 && timestamp - last_flush_ms >= FLUSH_INTERVAL_MS)) {
            flush_batch(&writer, batch, &batch_count, &last_flush_ms);
        }
        usleep(SAMPLE_INTERVAL_US);
    }
    if (batch_count > 0 && mysql_writer_write_batch(&writer, batch, batch_count) != 0)
        fprintf(stderr, "final database flush failed: %zu metrics not stored\n", batch_count);
    mysql_writer_close(&writer);
    printf("system monitor stopped\n");
    return 0;
}
