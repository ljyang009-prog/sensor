# 测试计划

本文档用于验证 `system_monitor` 在采集、传输、落库和部署环节的行为。

## 测试环境

- Ubuntu 24.04
- MySQL 8.0
- Redis 7.x
- GCC + libmysqlclient
- hiredis（虚拟机部署时使用用户目录安装）

## 1. 构建测试

```bash
cmake -S . -B build
cmake --build build
```

不使用 CMake 时：

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
```

通过标准：

- 编译无错误。
- 使用 `-Wall -Wextra` 时无未处理警告。

## 2. 核心功能测试

### 2.1 数据采集正确性

- CPU 总使用率与 `top -bn1` 基本一致。
- 每核 CPU 使用率与 `top -bn1 -H` 对比合理。
- 内存使用率与 `free -m` 计算结果一致。
- 首次采样只建立基线，不输出异常使用率。

```bash
top -bn1 | head -20
free -m
cat /proc/stat
cat /proc/meminfo
```

### 2.2 Redis Stream

```bash
redis-cli XLEN system_metrics
redis-cli XINFO STREAM system_metrics
redis-cli XINFO GROUPS system_metrics
```

通过标准：

- 消费组 `writers` 自动创建。
- 生产者持续写入 Stream。
- 消费者写入 MySQL 后执行 `XACK`。
- `MAXLEN` 生效，Stream 不会无限增长。

### 2.3 MySQL 写入

```bash
MYSQL_PWD=tom mysql -h 127.0.0.1 -P 3306 -u tom sensor_monitor \
  -e "SELECT COUNT(*) FROM system_metrics;"
```

通过标准：

- 首次启动自动创建 `system_metrics` 表。
- 每轮采样包含 `cpu/all`、各逻辑核和 `memory_usage`。
- 相同 `message_id` 重复插入不会产生重复行。
- 批量提交成功，事务回滚时不会写入半批数据。

### 2.4 本地兜底

本地兜底文件由程序自动生成：

```text
system_metrics.spill.log
```

通过标准：

- Redis 不可用时，采集数据追加写入 spill log。
- MySQL 不可用时，消费端暂停落库，数据先堆积在 Redis。
- 故障恢复后能继续处理，不产生重复数据。

## 3. 故障测试

### 3.1 MySQL 故障

```bash
sudo systemctl stop mysql
```

预期：

- 服务不退出。
- Redis Stream 消息持续累积或被 spill log 承接。

恢复：

```bash
sudo systemctl start mysql
```

预期：

- 服务自动重连 MySQL。
- 堆积消息批量写入数据库。
- `system_metrics` 记录数继续增长。

### 3.2 Redis 故障

```bash
sudo systemctl stop redis-server
```

预期：

- 服务不退出。
- 采集数据写入 spill log。

恢复：

```bash
sudo systemctl start redis-server
```

预期：

- 自动重连 Redis。
- 采集恢复写入 Redis Stream。

### 3.3 进程强杀

```bash
kill -9 <pid>
systemctl --user start system-monitor.service
```

预期：

- 重启后服务正常。
- Redis 中未确认消息可被重新消费。
- `message_id` 唯一约束保证不会重复落库。

### 3.4 优雅退出

```bash
systemctl --user stop system-monitor.service
```

预期：

- 进程在排空 Redis 剩余消息后退出。
- 服务状态变为 `inactive (dead)`。
- 不出现 `BLOCK 0` 导致的永久阻塞。

## 4. 长时间运行测试

建议连续运行至少 24 小时，记录：

- 进程内存是否持续增长。
- 进程 CPU 占用是否稳定。
- `system_metrics` 数据增长速度。
- Redis Stream 长度是否受 `MAXLEN` 限制。
- spill log 和日志文件大小。

```bash
systemctl --user status system-monitor.service
journalctl --user -u system-monitor.service -f
```

## 5. 性能测试

- 默认 500ms 采样。
- 将采样周期压到 100ms，观察 CPU 和 MySQL 写入延迟。
- 模拟多实例并发上报，确认单 MySQL 实例的吞吐上限。
- 验证批量阈值：按条数或时间触发，例如 1024 条或 1 秒。

## 6. 安全测试

- 确认所有 SQL 均使用 prepared statement。
- 数据库配置文件的权限为 `600`。
- 日志中不能出现数据库密码。
- MySQL 用户权限最小化，建议只授予：

```sql
GRANT SELECT, INSERT, CREATE, ALTER, INDEX ON sensor_monitor.* TO 'tom'@'%';
```

## 7. 自动化测试

当前项目还没有自动化测试，建议补充：

- CPU/内存解析单元测试。
- Redis Stream 消息解析单元测试。
- MySQL 批量写入测试。
- 集成测试：启动服务 → 等待几秒 → 查询 MySQL 有数据 → 停止服务 → 确认无残留进程。

## 8. 当前已知问题

- `tom` 目前是 `ALL PRIVILEGES`，权限过宽，需要收敛。
- `system_metrics` 没有保留策略，长时间运行会持续增长。
- user service 已启用，但重启后自动运行依赖 `loginctl enable-linger`。
- 故障注入、长跑和性能测试尚未完成。
- 本机代码仓库与虚拟机部署目录需要保持同步，避免两套代码漂移。
