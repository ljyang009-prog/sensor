#include "system_monitor/cpu_monitor.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 解析 /proc/stat 中一行CPU数据，提取核心编号、总时间、空闲时间
static int parse_cpu_line(const char *line, int *index,
                          unsigned long long *total,
                          unsigned long long *idle)
{
    /* user, nice, system, idle, iowait, irq, softirq, steal */
    unsigned long long values[8] = {0};
    const char *cursor = line;
    char *end;
    int fields = 0;
    if (strncmp(cursor, "cpu", 3) != 0)
        return 0;
    cursor += 3;
    if (isspace((unsigned char)*cursor))
    {
        *index = 0;
    }
    else
    {
        unsigned long core;
        if (!isdigit((unsigned char)*cursor))
            return 0;
        core = strtoul(cursor, &end, 10);
        if (end == cursor || !isspace((unsigned char)*end) ||
            core >= CPU_MONITOR_MAX_CORES)
            return 0;
        *index = (int)core + 1;
        cursor = end;
    }
    while (fields < 8)
    {
        while (isspace((unsigned char)*cursor))
            cursor++;
        if (*cursor == '\0')
            break;
        values[fields] = strtoull(cursor, &end, 10);
        if (end == cursor)
            return 0;
        fields++;
        cursor = end;
    }
    if (fields < 4)
        return 0;
    *total = values[0] + values[1] + values[2] + values[3];
    *idle = values[3];
    if (fields >= 5)
    {
        *total += values[4];
        *idle += values[4];
    }
    if (fields >= 6)
        *total += values[5];
    if (fields >= 7)
        *total += values[6];
    if (fields >= 8)
        *total += values[7];
    return 1;
}

static double usage_percent(cpu_jiffies_t old, cpu_jiffies_t current)
{
    if (current.total < old.total || current.idle < old.idle)
        return 0.0;
    unsigned long long total_delta = current.total - old.total;
    unsigned long long idle_delta = current.idle - old.idle;
    if (total_delta == 0 || idle_delta > total_delta)
        return 0.0;
    return ((double)(total_delta - idle_delta) * 100.0) / (double)total_delta;
}

static void fill_message(system_metric_t *message, const char *device_id,
                         double value, int64_t event_time_ms, uint64_t sequence)
{
    memset(message, 0, sizeof(*message));
    snprintf(message->message_id, sizeof(message->message_id),
#ifdef _WIN32
             "cpu-%I64d-%I64u", (long long)event_time_ms,
#else
             "cpu-%lld-%llu", (long long)event_time_ms,
#endif
             (unsigned long long)sequence);
    snprintf(message->device_id, sizeof(message->device_id), "%s", device_id);
    snprintf(message->metric_type, sizeof(message->metric_type), "cpu_usage");
    snprintf(message->unit, sizeof(message->unit), "percent");
    snprintf(message->schema_version, sizeof(message->schema_version), "1.0");
    message->event_time_ms = event_time_ms;
    message->value = value;
    message->sequence = (int64_t)sequence;
}

void cpu_monitor_init(cpu_monitor_t *monitor)
{
    if (!monitor)
        return;
    memset(monitor, 0, sizeof(*monitor));
}

int cpu_monitor_sample(cpu_monitor_t *monitor, system_metric_t *messages,
                       size_t capacity, size_t *message_count,
                       int64_t event_time_ms)
{
    FILE *file;
    char line[512];
    cpu_jiffies_t current[CPU_MONITOR_MAX_CORES + 1];
    int seen[CPU_MONITOR_MAX_CORES + 1] = {0};
    int index;
    size_t required = 0;
    unsigned long long total, idle;
    size_t count = 0;

    if (!monitor || !messages || !message_count || capacity == 0)
        return -1;
    *message_count = 0;
    memset(current, 0, sizeof(current));
    file = fopen("/proc/stat", "r");
    if (!file)
        return -1;
    while (fgets(line, sizeof(line), file))
    {
        if (!parse_cpu_line(line, &index, &total, &idle))
        {
            if (strncmp(line, "cpu", 3) != 0)
                break;
            continue;
        }
        current[index].total = total;
        current[index].idle = idle;
        current[index].present = 1;
        seen[index] = 1;
    }
    fclose(file);
    if (!seen[0])
        return -1;
    if (!monitor->initialized)
    {
        memcpy(monitor->previous, current, sizeof(current));
        monitor->initialized = 1;
        monitor->core_count = 0;
        for (index = 1; index <= CPU_MONITOR_MAX_CORES; ++index)
            if (seen[index])
                monitor->core_count++;
        return 1;
    }
    for (index = 0; index <= CPU_MONITOR_MAX_CORES; ++index)
        if (seen[index])
            required++;
    if (capacity < required)
        return -1;
    fill_message(&messages[count++], "cpu/all",
                 usage_percent(monitor->previous[0], current[0]),
                 event_time_ms, ++monitor->sequence);
    for (index = 1; index <= CPU_MONITOR_MAX_CORES; ++index)
    {
        char device_id[32];
        if (!seen[index])
            continue;
        snprintf(device_id, sizeof(device_id), "cpu/%d", index - 1);
        fill_message(&messages[count++], device_id,
                     monitor->previous[index].present
                         ? usage_percent(monitor->previous[index], current[index])
                         : 0.0,
                     event_time_ms, ++monitor->sequence);
    }
    memcpy(monitor->previous, current, sizeof(current));
    monitor->core_count = (int)required - 1;
    *message_count = count;
    return 0;
}
