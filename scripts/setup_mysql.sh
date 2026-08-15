#!/usr/bin/env bash
set -euo pipefail

config_dir="${HOME}/.config"
config_file="${config_dir}/system-monitor.env"
db_password="$(openssl rand -hex 24)"

sudo mysql <<SQL
CREATE DATABASE IF NOT EXISTS system_monitor CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'monitor_writer'@'127.0.0.1' IDENTIFIED BY '${db_password}';
ALTER USER 'monitor_writer'@'127.0.0.1' IDENTIFIED BY '${db_password}';
GRANT SELECT, INSERT, CREATE, ALTER, INDEX ON system_monitor.* TO 'monitor_writer'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

install -d -m 700 "${config_dir}"
umask 077
cat >"${config_file}" <<EOF
MONITOR_DB_HOST=127.0.0.1
MONITOR_DB_PORT=3306
MONITOR_DB_NAME=system_monitor
MONITOR_DB_USER=monitor_writer
MONITOR_DB_PASSWORD=${db_password}
REDIS_HOST=127.0.0.1
REDIS_PORT=6379
REDIS_STREAM=system_metrics
REDIS_GROUP=writers
REDIS_MAXLEN=100000
EOF
chmod 600 "${config_file}"
unset db_password

echo "MySQL database and user are ready."
echo "Configuration saved to ${config_file} (mode 600)."
