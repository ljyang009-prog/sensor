#!/usr/bin/env bash
set -euo pipefail

config_dir="${HOME}/.config"
config_file="${config_dir}/sensor-monitor.env"
db_password="$(openssl rand -hex 24)"

sudo mysql <<SQL
CREATE DATABASE IF NOT EXISTS sensor CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER IF NOT EXISTS 'sensor_writer'@'127.0.0.1' IDENTIFIED BY '${db_password}';
ALTER USER 'sensor_writer'@'127.0.0.1' IDENTIFIED BY '${db_password}';
GRANT SELECT, INSERT, CREATE, ALTER, INDEX ON sensor.* TO 'sensor_writer'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

install -d -m 700 "${config_dir}"
umask 077
cat >"${config_file}" <<EOF
SENSOR_DB_HOST=127.0.0.1
SENSOR_DB_PORT=3306
SENSOR_DB_NAME=sensor
SENSOR_DB_USER=sensor_writer
SENSOR_DB_PASSWORD=${db_password}
EOF
chmod 600 "${config_file}"
unset db_password

echo "MySQL database and user are ready."
echo "Configuration saved to ${config_file} (mode 600)."
