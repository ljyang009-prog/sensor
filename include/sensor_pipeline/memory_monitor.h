#ifndef SENSOR_PIPELINE_MEMORY_MONITOR_H
#define SENSOR_PIPELINE_MEMORY_MONITOR_H

#include "sensor_message.h"
#include <stdint.h>

int memory_monitor_sample(sensor_message_t *message, int64_t event_time_ms,
                          uint64_t sequence);

#endif
