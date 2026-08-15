#ifndef SYSTEM_MONITOR_SYSTEM_METRIC_H
#define SYSTEM_MONITOR_SYSTEM_METRIC_H

#include <stdint.h>

#define METRIC_ID_LEN 64
#define METRIC_TYPE_LEN 64
#define METRIC_UNIT_LEN 16
#define METRIC_SCHEMA_LEN 16

typedef struct {
    char message_id[64];
    char device_id[METRIC_ID_LEN];
    char metric_type[METRIC_TYPE_LEN];
    int64_t event_time_ms;
    double value;
    char unit[METRIC_UNIT_LEN];
    int64_t sequence;
    char schema_version[METRIC_SCHEMA_LEN];
} system_metric_t;

#endif
