#ifndef SYSTEM_MONITOR_MEMORY_MONITOR_H
#define SYSTEM_MONITOR_MEMORY_MONITOR_H

#include "system_metric.h"
#include <stdint.h>

int memory_monitor_sample(system_metric_t *message, int64_t event_time_ms,
                          uint64_t sequence);

#endif
