#include "sensor_pipeline/memory_monitor.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define U64_FORMAT "%I64u"
#else
#define U64_FORMAT "%llu"
#endif

int memory_monitor_sample(sensor_message_t *message, int64_t event_time_ms,
                          uint64_t sequence) {
    FILE *file;
    char line[256];
    unsigned long long total_kb = 0;
    unsigned long long available_kb = 0;

    if (!message) return -1;
    file = fopen("/proc/meminfo", "r");
    if (!file) return -1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: " U64_FORMAT " kB", &total_kb) == 1) continue;
        if (sscanf(line, "MemAvailable: " U64_FORMAT " kB", &available_kb) == 1) continue;
        if (total_kb > 0 && available_kb > 0) break;
    }
    fclose(file);
    if (total_kb == 0 || available_kb > total_kb) return -1;

    memset(message, 0, sizeof(*message));
#ifdef _WIN32
    snprintf(message->message_id, sizeof(message->message_id),
             "memory-%I64d-%I64u", (long long)event_time_ms,
             (unsigned long long)sequence);
#else
    snprintf(message->message_id, sizeof(message->message_id),
             "memory-%lld-%llu", (long long)event_time_ms,
             (unsigned long long)sequence);
#endif
    snprintf(message->device_id, sizeof(message->device_id), "host");
    snprintf(message->sensor_type, sizeof(message->sensor_type), "memory_usage");
    snprintf(message->unit, sizeof(message->unit), "percent");
    snprintf(message->schema_version, sizeof(message->schema_version), "1.0");
    message->event_time_ms = event_time_ms;
    message->value = ((double)(total_kb - available_kb) * 100.0) /
                     (double)total_kb;
    message->sequence = (int64_t)sequence;
    return 0;
}
