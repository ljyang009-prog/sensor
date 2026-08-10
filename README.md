# sensor_pipeline

当前阶段只保留 Linux 系统 CPU 和内存使用率采集。

## 构建运行

```bash
cmake -S . -B build
cmake --build build
./build/system_monitor_app
```

程序读取 `/proc/stat` 和 `/proc/meminfo`，每 500ms 输出一次总 CPU、各逻辑核 CPU 和内存使用率。第一次 CPU 采样只建立基线，不输出 CPU 使用率。
