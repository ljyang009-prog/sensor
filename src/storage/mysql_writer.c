#include "sensor_pipeline/mysql_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *env_or_default(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}

static int prepare_schema(mysql_writer_t *writer) {
    static const char create_table_sql[] =
        "CREATE TABLE IF NOT EXISTS system_metrics ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "message_id VARCHAR(64) NOT NULL,"
        "device_id VARCHAR(64) NOT NULL,"
        "sensor_type VARCHAR(64) NOT NULL,"
        "event_time_ms BIGINT NOT NULL,"
        "value DOUBLE NOT NULL,"
        "unit VARCHAR(16) NOT NULL,"
        "sequence_no BIGINT NOT NULL,"
        "schema_version VARCHAR(16) NOT NULL,"
        "created_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),"
        "UNIQUE KEY uk_message_id (message_id),"
        "KEY idx_type_device_time (sensor_type, device_id, event_time_ms)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    static const char insert_sql[] =
        "INSERT IGNORE INTO system_metrics "
        "(message_id,device_id,sensor_type,event_time_ms,value,unit,sequence_no,schema_version) "
        "VALUES (?,?,?,?,?,?,?,?)";

    if (mysql_query(writer->connection, create_table_sql) != 0) {
        fprintf(stderr, "create table failed: %s\n", mysql_error(writer->connection));
        return -1;
    }
    writer->insert_statement = mysql_stmt_init(writer->connection);
    if (!writer->insert_statement ||
        mysql_stmt_prepare(writer->insert_statement, insert_sql,
                           (unsigned long)strlen(insert_sql)) != 0) {
        fprintf(stderr, "prepare insert failed: %s\n",
                writer->insert_statement
                    ? mysql_stmt_error(writer->insert_statement)
                    : mysql_error(writer->connection));
        return -1;
    }
    return 0;
}

int mysql_writer_open(mysql_writer_t *writer) {
    const char *host, *database, *user, *password;
    unsigned int port;
    char *port_end;
    unsigned long parsed_port;

    if (!writer) return -1;
    memset(writer, 0, sizeof(*writer));
    host = env_or_default("SENSOR_DB_HOST", "127.0.0.1");
    database = env_or_default("SENSOR_DB_NAME", "sensor");
    user = env_or_default("SENSOR_DB_USER", "sensor_writer");
    password = getenv("SENSOR_DB_PASSWORD");
    if (!password) {
        fprintf(stderr, "SENSOR_DB_PASSWORD is not set\n");
        return -1;
    }
    parsed_port = strtoul(env_or_default("SENSOR_DB_PORT", "3306"), &port_end, 10);
    if (*port_end != '\0' || parsed_port == 0 || parsed_port > 65535) {
        fprintf(stderr, "invalid SENSOR_DB_PORT\n");
        return -1;
    }
    port = (unsigned int)parsed_port;
    writer->connection = mysql_init(NULL);
    if (!writer->connection) return -1;
    if (!mysql_real_connect(writer->connection, host, user, password, database,
                            port, NULL, 0)) {
        fprintf(stderr, "mysql connect failed: %s\n", mysql_error(writer->connection));
        mysql_writer_close(writer);
        return -1;
    }
    if (mysql_set_character_set(writer->connection, "utf8mb4") != 0) {
        fprintf(stderr, "set character set failed: %s\n",
                mysql_error(writer->connection));
        mysql_writer_close(writer);
        return -1;
    }
    if (prepare_schema(writer) != 0) {
        mysql_writer_close(writer);
        return -1;
    }
    printf("connected to MySQL %s:%u/%s\n", host, port, database);
    return 0;
}

static int execute_insert(mysql_writer_t *writer, const sensor_message_t *message) {
    MYSQL_BIND bind[8];
    unsigned long lengths[8];
    long long event_time = (long long)message->event_time_ms;
    long long sequence = (long long)message->sequence;
    double value = message->value;
    const char *strings[5] = {message->message_id, message->device_id,
                              message->sensor_type, message->unit,
                              message->schema_version};
    int positions[5] = {0, 1, 2, 5, 7};
    int i;

    memset(bind, 0, sizeof(bind));
    memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < 5; ++i) {
        int position = positions[i];
        lengths[position] = (unsigned long)strlen(strings[i]);
        bind[position].buffer_type = MYSQL_TYPE_STRING;
        bind[position].buffer = (void *)strings[i];
        bind[position].buffer_length = lengths[position];
        bind[position].length = &lengths[position];
    }
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = &event_time;
    bind[4].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[4].buffer = &value;
    bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[6].buffer = &sequence;
    if (mysql_stmt_bind_param(writer->insert_statement, bind) != 0 ||
        mysql_stmt_execute(writer->insert_statement) != 0) {
        fprintf(stderr, "insert failed: %s\n", mysql_stmt_error(writer->insert_statement));
        return -1;
    }
    return 0;
}

int mysql_writer_write_batch(mysql_writer_t *writer,
                             const sensor_message_t *messages, size_t count) {
    size_t i;
    if (!writer || !writer->connection || !writer->insert_statement ||
        (!messages && count > 0)) return -1;
    if (count == 0) return 0;
    if (mysql_ping(writer->connection) != 0) {
        fprintf(stderr, "mysql ping failed: %s\n", mysql_error(writer->connection));
        return -1;
    }
    if (mysql_autocommit(writer->connection, 0) != 0) {
        fprintf(stderr, "disable autocommit failed: %s\n",
                mysql_error(writer->connection));
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (execute_insert(writer, &messages[i]) != 0) {
            mysql_rollback(writer->connection);
            mysql_autocommit(writer->connection, 1);
            return -1;
        }
    }
    if (mysql_commit(writer->connection) != 0) {
        fprintf(stderr, "commit failed: %s\n", mysql_error(writer->connection));
        mysql_rollback(writer->connection);
        mysql_autocommit(writer->connection, 1);
        return -1;
    }
    mysql_autocommit(writer->connection, 1);
    return 0;
}

void mysql_writer_close(mysql_writer_t *writer) {
    if (!writer) return;
    if (writer->insert_statement) mysql_stmt_close(writer->insert_statement);
    if (writer->connection) mysql_close(writer->connection);
    memset(writer, 0, sizeof(*writer));
}
