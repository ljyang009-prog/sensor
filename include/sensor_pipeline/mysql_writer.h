#ifndef SENSOR_PIPELINE_MYSQL_WRITER_H
#define SENSOR_PIPELINE_MYSQL_WRITER_H

#include "sensor_message.h"
#include <mysql.h>
#include <stddef.h>

typedef struct {
    MYSQL *connection;
    MYSQL_STMT *insert_statement;
} mysql_writer_t;

int mysql_writer_open(mysql_writer_t *writer);
int mysql_writer_write_batch(mysql_writer_t *writer,
                             const sensor_message_t *messages,
                             size_t count);
void mysql_writer_close(mysql_writer_t *writer);

#endif
