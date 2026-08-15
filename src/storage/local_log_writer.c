#include "system_monitor/local_log_writer.h"
#include <inttypes.h>
#include <string.h>

int local_log_open(local_log_writer_t *writer, const char *path) {
    if (!writer || !path || !*path) return -1;
    memset(writer, 0, sizeof(*writer));
    writer->file = fopen(path, "a");
    if (!writer->file) return -1;
    pthread_mutex_init(&writer->mutex, NULL);
    return 0;
}

/* 前提：持有互斥锁 */
static int write_one_locked(local_log_writer_t *writer,
                            const system_metric_t *message) {
    /* 一行一条，竖线分隔，便于后续解析或重放 */
    return fprintf(writer->file,
                   "%s|%s|%s|%" PRId64 "|%.6f|%s|%" PRId64 "|%s\n",
                   message->message_id, message->device_id,
                   message->metric_type, message->event_time_ms,
                   message->value, message->unit, message->sequence,
                   message->schema_version) < 0
               ? -1
               : 0;
}

int local_log_write(local_log_writer_t *writer,
                    const system_metric_t *message) {
    int rc;
    if (!writer || !writer->file || !message) return -1;
    pthread_mutex_lock(&writer->mutex);
    rc = write_one_locked(writer, message);
    if (rc == 0) fflush(writer->file);
    pthread_mutex_unlock(&writer->mutex);
    return rc;
}

int local_log_write_batch(local_log_writer_t *writer,
                          const system_metric_t *messages, size_t count) {
    size_t i;
    int rc = 0;
    if (!writer || !writer->file || (!messages && count > 0)) return -1;
    pthread_mutex_lock(&writer->mutex);
    for (i = 0; i < count; ++i) {
        if (write_one_locked(writer, &messages[i]) != 0) {
            rc = -1;
            break;
        }
    }
    if (fflush(writer->file) != 0) rc = -1;
    pthread_mutex_unlock(&writer->mutex);
    return rc;
}

void local_log_close(local_log_writer_t *writer) {
    if (!writer) return;
    if (writer->file) {
        fclose(writer->file);
        writer->file = NULL;
    }
    pthread_mutex_destroy(&writer->mutex);
}
