# linux-system-monitor

Linux CPU 和内存使用率采集程序：采集指标写入 Redis Stream（消息队列削峰），消费端批量写入 MySQL。

## 依赖

Debian/Ubuntu：

```bash
sudo apt install build-essential cmake default-libmysqlclient-dev libhiredis-dev
```

Fedora/RHEL：

```bash
sudo dnf install gcc cmake mariadb-connector-c-devel hiredis-devel
```

## 数据库

先创建数据库和最小权限用户：

```sql
CREATE DATABASE system_monitor CHARACTER SET utf8mb4;
CREATE USER 'monitor_writer'@'localhost' IDENTIFIED BY 'change-me';
GRANT SELECT, INSERT, CREATE, ALTER, INDEX ON system_monitor.* TO 'monitor_writer'@'localhost';
```

程序会自动创建 `system_metrics` 表。连接配置由环境变量提供：

```bash
export MONITOR_DB_HOST=127.0.0.1
export MONITOR_DB_PORT=3306
export MONITOR_DB_NAME=system_monitor
export MONITOR_DB_USER=monitor_writer
export MONITOR_DB_PASSWORD='change-me'
```

## Redis Stream

采集到的指标先写入 Redis Stream，再由消费端批量写入 MySQL。连接配置：

```bash
export REDIS_HOST=127.0.0.1
export REDIS_PORT=6379
export REDIS_STREAM=system_metrics
export REDIS_GROUP=writers
export REDIS_MAXLEN=100000
```

`REDIS_MAXLEN` 限制 Stream 最大长度，防止消息堆积导致内存暴涨。

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
  src/mq/redis_stream.c \
  src/storage/mysql_writer.c \
  src/storage/local_log_writer.c \
  apps/system_monitor_main.c \
  $(mysql_config --cflags --libs) \
  -lpthread -lhiredis \
  -o system_monitor_app
./system_monitor_app
```

## 运行架构

程序采用“采集线程 → Redis Stream → DB 写线程”的削峰模型：

- **采集线程**：每 500ms 读取 `/proc/stat` 和 `/proc/meminfo`，采集总 CPU、各逻辑核 CPU 和内存使用率，`XADD` 写入 Redis Stream。
- **Redis Stream**：作为采集与落库之间的消息队列（削峰/解耦），`MAXLEN` 限制长度防止内存暴涨；消费组提供“写库成功后再 `XACK`”的至少一次投递。
- **DB 写线程**：`XREADGROUP` 批量读取（最多 1024 条、阻塞最长 1 秒），再 `mysql_stmt` 批量写入 MySQL；`message_id` 唯一索引 + `INSERT IGNORE` 用于幂等写入。
- **本地兜底**：Redis 不可用或数据库写入失败时，消息追加写入 `system_metrics.spill.log`，避免直接丢失。
- **优雅退出**：`SIGINT`/`SIGTERM` 置退出标志，采集线程停止后 DB 线程排空 Redis 剩余消息再退出。

验证数据：

```sql
SELECT metric_type, device_id, value, unit, event_time_ms
FROM system_metrics
ORDER BY id DESC
LIMIT 20;
```

## systemd 用户服务

先初始化数据库配置，并把构建产物安装到服务使用的目录：

```bash
bash scripts/setup_mysql.sh
mkdir -p ~/monitor_deploy
install -m 755 build/system_monitor_app ~/monitor_deploy/system_monitor_app
```

再安装并启动用户服务：

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
