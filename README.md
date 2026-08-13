# sensor_pipeline

Linux CPU 和内存使用率采集程序，使用 MySQL/MariaDB C API 批量写入数据库。

## 依赖

Debian/Ubuntu：

```bash
sudo apt install build-essential cmake default-libmysqlclient-dev
```

Fedora/RHEL：

```bash
sudo dnf install gcc cmake mariadb-connector-c-devel
```

## 数据库

先创建数据库和最小权限用户：

```sql
CREATE DATABASE sensor CHARACTER SET utf8mb4;
CREATE USER 'sensor_writer'@'localhost' IDENTIFIED BY 'change-me';
GRANT SELECT, INSERT, CREATE, ALTER, INDEX ON sensor.* TO 'sensor_writer'@'localhost';
```

程序会自动创建 `system_metrics` 表。连接配置由环境变量提供：

```bash
export SENSOR_DB_HOST=127.0.0.1
export SENSOR_DB_PORT=3306
export SENSOR_DB_NAME=sensor
export SENSOR_DB_USER=sensor_writer
export SENSOR_DB_PASSWORD='change-me'
```

## 构建运行

```bash
cmake -S . -B build
cmake --build build
./build/system_monitor_app
```

不使用 CMake 时，可通过客户端工具提供编译参数：

```bash
gcc -std=c99 -Wall -Wextra -Iinclude \
  src/collector/cpu_monitor.c \
  src/collector/memory_monitor.c \
  src/storage/mysql_writer.c \
  apps/system_monitor_main.c \
  $(mysql_config --cflags --libs) \
  -o system_monitor_app
./system_monitor_app
```

程序每 500ms 读取 `/proc/stat` 和 `/proc/meminfo`，采集总 CPU、各逻辑核 CPU 和内存使用率。最多累计 1024 条或每 1 秒提交一次 MySQL 事务，`message_id` 唯一索引用于幂等写入。

验证数据：

```sql
SELECT sensor_type, device_id, value, unit, event_time_ms
FROM system_metrics
ORDER BY id DESC
LIMIT 20;
```

## systemd 用户服务

```bash
mkdir -p ~/.config/systemd/user
cp deploy/system-monitor.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now system-monitor.service
sudo loginctl enable-linger "$USER"
```

查看状态和日志：

```bash
systemctl --user status system-monitor.service
journalctl --user -u system-monitor.service -f
```
