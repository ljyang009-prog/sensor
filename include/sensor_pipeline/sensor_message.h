#ifndef SENSOR_PIPELINE_SENSOR_MESSAGE_H
#define SENSOR_PIPELINE_SENSOR_MESSAGE_H

#include <stdint.h>

#define SENSOR_ID_LEN 64
#define SENSOR_TYPE_LEN 64
#define SENSOR_UNIT_LEN 16
#define SENSOR_SCHEMA_LEN 16

typedef struct {
    char message_id[64];
    char device_id[SENSOR_ID_LEN];
    char sensor_type[SENSOR_TYPE_LEN];
    int64_t event_time_ms;
    double value;
    char unit[SENSOR_UNIT_LEN];
    int64_t sequence;
    char schema_version[SENSOR_SCHEMA_LEN];
} sensor_message_t;

#endif
