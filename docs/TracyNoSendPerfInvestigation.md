# Tracy NoSend 宏与导出性能探查归档

## 1. 背景
原 `PerformanceTests1/TracyNoSendPerfTest.cpp` 在 VSTest/testhost 中执行时，可能出现“日志打印后长时间不结束”的现象。该现象通常与 testhost 复用、调试采集、DLL 占用相关，而非单纯编译问题。

为避免框架干扰，已新增 `examples/NoSendPerf`，提供可独立运行的 DLL + Runner。

## 2. 新增工件
- `examples/NoSendPerf/TracyNoSendPerfCase.cpp`
  - `TracyNoSend_RunZoneBeginEndPerf`
  - `TracyNoSend_RunExportToFilePerf`
- `examples/NoSendPerf/TracyNoSendPerfRunner.cpp`
  - 直接调用上述导出函数并打印指标
- `examples/NoSendPerf/CMakeLists.txt`
- `examples/CMakeLists.txt` 已 `add_subdirectory(NoSendPerf)`

## 3. 构建与运行命令（Ninja）
在仓库根目录执行：

```powershell
cmake -S . -B out/perf-nosend -G Ninja -DTRACY_BUILD_EXAMPLES=ON -DTRACY_SAVE_NO_SEND=ON
cmake --build out/perf-nosend --target TracyNoSendPerfCase TracyNoSendPerfRunner -j
./out/perf-nosend/examples/NoSendPerf/TracyNoSendPerfRunner.exe
```

> 注意：如果在现有目录（如 `out/all-hcomm-release`）看到 `LNK4272 x86/x64 冲突`，说明该目录架构与当前终端工具链不一致。请新建独立 x64 构建目录重新配置。

## 4. 指标口径
- Zone 开销：`ZoneBegin + ZoneEnd` 一对操作的平均耗时（纯 zone 路径，无额外事件）
  - `ns_per_pair = elapsed_ns / iterations`
- 写文件性能：`PerfettoNativeExporter::ExportToFile`
  - 每迭代产生 7 个事件：`ZoneBegin` + `Instant` + `Counter(int64)` + `Counter(double)` + `MemAlloc` + `MemFree` + `ZoneEnd`
  - `elapsed_us`：导出耗时
  - `file_size`：输出 trace 文件大小
  - `throughput_mb_s = (file_size / MiB) / (elapsed_us / 1e6)`

## 5. 目前已观测数据
### 5.1 测试日志（MSTest）
- `zone begin/end elapsed_ns=164904000`
- `iterations=200000`
- `ns_per_pair=824.52`

### 5.2 CPU Profiling（ZoneBeginEndOverheadPerf）
- `tracylite::PerfettoNativeExporter::ExportToBuffer`：`74.07%`
- `tracylite::Collector::ZoneBegin`：`3.15%`
- `tracylite::Collector::ZoneEnd`：`0.10%`

结论：该场景主耗时在导出序列化链路（`CollectTrace/AppendTracePacket`），不是 Begin/End 本体。

### 5.3 环境指标（Runner 输出）
- `cpu_count`：由 `std::thread::hardware_concurrency()` 输出
- `threadCount`：当前写文件测试使用 `4`

### 5.4 CPU Profiling（ExportToFilePerf）
- `tracylite::PerfettoNativeExporter::ExportToFile`：`47.02%`
- 其主要子路径：`CollectTrace`（`46.91%`）

## 6. 建议的后续采样格式
建议每次运行记录：

```text
时间戳, CPU核心数, threadCount, iterationsPerThread, zone_elapsed_ns, zone_ns_per_pair, export_elapsed_us, file_size_bytes, throughput_mb_s
```

可先用 Runner 标准输出收集，再追加到 CSV/MD。
