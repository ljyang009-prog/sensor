#ifndef SYSTEM_MONITOR_CPU_MONITOR_H
#define SYSTEM_MONITOR_CPU_MONITOR_H

#include "system_metric.h"
#include <stddef.h>
#include <stdint.h>

#define CPU_MONITOR_MAX_CORES 256

typedef struct {
    unsigned long long total;
    unsigned long long idle;
    int present;
} cpu_jiffies_t;

typedef struct {
    cpu_jiffies_t previous[CPU_MONITOR_MAX_CORES + 1]; /* 0=all, 1..N=cpuN */
    int initialized;
    int core_count;
    uint64_t sequence;
} cpu_monitor_t;

void cpu_monitor_init(cpu_monitor_t *monitor);

/*
 * 读取一次 /proc/stat。
 * 返回值：0=产生消息，1=首次采样尚无使用率，-1=读取或解析失败。
 * messages[0] 为总 CPU，后续为各逻辑核；capacity 至少为 MAX_CORES+1。
 */
int cpu_monitor_sample(cpu_monitor_t *monitor,
                       system_metric_t *messages,
                       size_t capacity,
                       size_t *message_count,
                       int64_t event_time_ms);

#endif
