#include "sensor_pipeline/cpu_monitor.h"
#include "sensor_pipeline/memory_monitor.h"
#include <inttypes.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop_monitor(int signal_number) { (void)signal_number; running = 0; }

static int64_t now_ms(void) {
    struct timeval value;
    gettimeofday(&value, NULL);
    return (int64_t)value.tv_sec * 1000 + value.tv_usec / 1000;
}

int main(void) {
    cpu_monitor_t cpu;
    sensor_message_t messages[CPU_MONITOR_MAX_CORES + 1];
    size_t count, i;
    uint64_t sequence = 1;
    cpu_monitor_init(&cpu);
    signal(SIGINT, stop_monitor);
    signal(SIGTERM, stop_monitor);
    printf("system monitor started, sampling every 500ms\n");

    while (running) {
        int64_t timestamp = now_ms();
        int result = cpu_monitor_sample(&cpu, messages,
                                        CPU_MONITOR_MAX_CORES + 1,
                                        &count, timestamp);
        if (result == 0) {
            for (i = 0; i < count; ++i)
                printf("type=%s device=%s value=%.2f %s time=%" PRId64 "\n",
                       messages[i].sensor_type, messages[i].device_id,
                       messages[i].value, messages[i].unit,
                       messages[i].event_time_ms);
        } else if (result < 0) {
            fprintf(stderr, "failed to read /proc/stat\n");
        }
        sensor_message_t memory;
        if (memory_monitor_sample(&memory, timestamp, sequence++) == 0)
            printf("type=%s device=%s value=%.2f %s time=%" PRId64 "\n",
                   memory.sensor_type, memory.device_id, memory.value,
                   memory.unit, memory.event_time_ms);
        else
            fprintf(stderr, "failed to read /proc/meminfo\n");
        usleep(500000);
    }
    printf("system monitor stopped\n");
    return 0;
}
