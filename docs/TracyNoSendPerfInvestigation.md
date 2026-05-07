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

## 7. 修复后基线（Counter 改造 + Release/PGO 对比）

环境：Windows / MSVC `19.50.35730` (`VS 2026 Enterprise 18.5.2`) / x64 / Ninja，
`cpu_count = 32`，迭代参数与 §4 一致（`zone iterations = 200000`，`export threadCount = 4`，`iterationsPerThread = 10000`）。

测量后的 5 轮结果（按平均值聚合）：

| 指标 | Release `out/nosend-release` | PGO `out/nosend-pgo` | PGO 收益 |
|---|---:|---:|---:|
| `zone ns_per_pair` | `19.19 ns` | `16.04 ns` | **~16.4% 更快** |
| `export total_elapsed_us` | `117,901 us` | `90,372 us` | **~23.3% 更快** |
| `serialize_us` | `103,928 us` | `75,069 us` | **~27.8% 更快** |
| `write_us` | `14,773 us` | `15,303 us` | I/O 抖动主导，~3.6% 偏慢 |
| `file_size` | `~16.3 MB` | `~16.3 MB` | 体积一致（counter 改为进程级 track 不影响事件数） |

要点：

- counter 轨道改造（per-thread → process-level，UI 显示问题修复）只改动 `PerfettoNativeExporter` 序列化阶段，**没有触及 `Collector::ZoneBegin/ZoneEnd`、`RingBuffer`、`StringTable`、TLS** 任何热路径字段。
- Begin/End 本体在 Release 下仍维持在 `< 20 ns/pair`，PGO 后进一步压到 `~16 ns/pair`，相对早期 MSTest `824.52 ns/pair` 档位无任何回退。
- 导出路径（`CollectTrace + AppendTracePacket`）正是 §5.2 / §5.4 中 CPU 占比最高的部分，PGO 在此处吃到了最明显的红利（~28%）。
- `write_us` 受文件系统/缓存抖动影响明显，单轮波动大，应以 `total_elapsed_us` + `serialize_us` 作为稳定信号。

## 8. PGO 编译流程（以本用例为准）

> 本节给出在本仓库内对 `TracyNoSendPerfCase`（DLL）+ `TracyNoSendPerfRunner`（EXE）做一次完整 MSVC PGO 的可复现步骤。
> 必须先进入 **x64 Visual Studio 开发者环境**（PowerShell 中通过 `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`，或 cmd 中 `VsDevCmd.bat -arch=x64 -host_arch=x64`）。

### 8.1 阶段 A：插桩构建（PGINSTRUMENT）

```powershell
cmake -S . -B out/nosend-pgo -G Ninja `
  -DCPM_SOURCE_CACHE=E:/source/tracy/.cpm-cache `
  -DTRACY_BUILD_EXAMPLES=ON -DTRACY_SAVE_NO_SEND=ON `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_FLAGS_RELEASE="/O2 /Ob3 /DNDEBUG /GL" `
  -DCMAKE_C_FLAGS_RELEASE="/O2 /Ob3 /DNDEBUG /GL" `
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/LTCG:PGINSTRUMENT /GENPROFILE" `
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="/LTCG:PGINSTRUMENT /GENPROFILE"

cmake --build out/nosend-pgo --target TracyNoSendPerfCase TracyNoSendPerfRunner -j
```

关键点：

- 编译期必须开 `/GL`（whole-program optimization），否则链接器拒绝写入 `.pgd`。
- 链接期 `/LTCG:PGINSTRUMENT /GENPROFILE` 会在 DLL/EXE 旁生成 `.pgd` 文件并启用插桩。
- 由于 `Perfetto SDK` 走在线获取在受限网络下会失败，这里复用仓库根 `.cpm-cache` 命中本地缓存（CMake 会回落到 `scripts/perfetto-sdk-offline`）。

### 8.2 阶段 B：训练运行（生成 `.pgc`）

```powershell
$exe = 'E:\source\tracy\out\nosend-pgo\examples\NoSendPerf\TracyNoSendPerfRunner.exe'
1..5 | ForEach-Object { & $exe zone; & $exe export }
```

校验产物：

```powershell
Get-ChildItem out/nosend-pgo -Recurse -Filter *.pgc | Measure-Object | % Count
```

输出非 0 即说明插桩成功且采集到 profile 数据。训练负载应尽量贴近真实场景，否则 PGO 的「分支命中假设」可能与生产路径不一致。

### 8.3 阶段 C：使用 profile 重新构建（PGOPTIMIZE）

```powershell
cmake -S . -B out/nosend-pgo -G Ninja `
  -DCPM_SOURCE_CACHE=E:/source/tracy/.cpm-cache `
  -DTRACY_BUILD_EXAMPLES=ON -DTRACY_SAVE_NO_SEND=ON `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_FLAGS_RELEASE="/O2 /Ob3 /DNDEBUG /GL" `
  -DCMAKE_C_FLAGS_RELEASE="/O2 /Ob3 /DNDEBUG /GL" `
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/LTCG:PGOPTIMIZE /USEPROFILE" `
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="/LTCG:PGOPTIMIZE /USEPROFILE"

cmake --build out/nosend-pgo --target TracyNoSendPerfCase TracyNoSendPerfRunner -j
```

此阶段链接器会读取 `.pgd` + `.pgc`，把热点函数布局/内联/分支预测重新摆放。

### 8.4 阶段 D：复测对比

```powershell
$exe = 'E:\source\tracy\out\nosend-pgo\examples\NoSendPerf\TracyNoSendPerfRunner.exe'
1..5 | ForEach-Object { & $exe zone; & $exe export }
```

把 `ns_per_pair`、`total_elapsed_us`、`serialize_us` 与 §7 的 Release 列做差即可得到收益。

### 8.5 常见坑

- **`LNK4272 architecture mismatch`**：当前 shell 不是 x64，必须用 `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`。
- **PGO 重构建时仍走 PGINSTRUMENT**：CMake 会缓存上一次的 `*_LINKER_FLAGS_RELEASE`，重新 `cmake -S . -B` 时必须显式覆盖（如 §8.3 所示）。
- **`.pgc` 生成数为 0**：通常是训练阶段进程没有干净退出（如崩溃 / `TerminateProcess`），或者目标不是用 `/GL` 编译的。
- **PGO 收益不明显**：训练负载与真实负载差异过大，或热点已经被前端编译期内联吃完，PGO 无可优化空间。
- **CRT/iterator debug**：PGO 与 `_ITERATOR_DEBUG_LEVEL != 0` 不友好，确保 Release 配置使用默认 `_ITERATOR_DEBUG_LEVEL=0`。

### 8.6 GCC 等价做法（备查）

GCC/Clang 上的对应链路：

```bash
# 阶段 A：插桩
g++ -O3 -flto -fprofile-generate=./pgo-data ...

# 阶段 B：训练
./TracyNoSendPerfRunner zone && ./TracyNoSendPerfRunner export

# 阶段 C：使用 profile
g++ -O3 -flto -fprofile-use=./pgo-data -fprofile-correction ...
```

进阶：Linux 上还可以走 `AutoFDO`（基于 `perf record` 采样而非插桩），训练侵入更低，但需要带 `LBR` 的 CPU 与 `create_gcov` 工具链支撑。

## 9. WSL2（clock_gettime）对照采样（2026-05-07）

环境：WSL2 Linux / `wsl2-all-hcomm-release` 全量构建（含 `--clean-first` 验证），
`cpu_count = 16`。

计时路径：配置日志显示 `TRACY_TIMER_FALLBACK=ON`，Linux 侧走 `clock_gettime`（`CLOCK_MONOTONIC_RAW`）路径。

### 9.1 Runner 参数化

`examples/NoSendPerf/TracyNoSendPerfRunner` 已支持可选参数：

- `zone [iterations]`
- `export [threadCount] [iterationsPerThread]`

默认仍保持：`zone=200000`、`export threadCount=4`、`iterationsPerThread=10000`。

### 9.2 采样命令

```bash
# baseline
./out/wsl2-all-hcomm-release/examples/NoSendPerf/TracyNoSendPerfRunner zone 1000000

# pin 到 CPU2
taskset -c 2 ./out/wsl2-all-hcomm-release/examples/NoSendPerf/TracyNoSendPerfRunner zone 1000000
```

统计口径：

- `zone`：采样 20 轮，统计 `ns_per_pair`
- `export`：采样 12 轮，统计 `total_elapsed_us`
- 分位点计算：排序后取 `idx = floor((N-1)*p) + 1`

### 9.3 结果（均值 / P95 / P99）

| 用例 | N | 均值 | P95 | P99 | min | max |
|---|---:|---:|---:|---:|---:|---:|
| `zone_1e6_baseline` (`ns_per_pair`) | 20 | `42.509` | `45.170` | `45.170` | `41.44` | `47.97` |
| `zone_1e6_affinity_cpu2` (`ns_per_pair`) | 20 | `42.906` | `45.680` | `45.680` | `42.08` | `45.70` |
| `export_baseline` (`total_elapsed_us`) | 12 | `108,876.50` | `121,857` | `121,857` | `97,490` | `122,441` |
| `export_affinity_cpu2` (`total_elapsed_us`) | 12 | `117,549.08` | `129,804` | `129,804` | `100,422` | `129,981` |

### 9.4 观察

- 在 WSL2 + `clock_gettime` 路径下，`zone` 维持在 `~42-43 ns/pair`，单次 begin/end 约为 `~21 ns` 量级。
- `taskset -c 2` 在本次环境中没有带来更优结果（均值与尾延迟略高），说明抖动更可能来自宿主调度与虚拟化层，而非仅线程迁移。
- `export` 仍主要受序列化 + 文件系统缓存行为影响，尾部延迟存在正常波动，建议长期追踪优先看 `avg + P95`。

### 9.5 当前结论

从该批数据看，NoSend 场景下的打点本体开销已非常低，处于常见高性能 profiler/tracing 库的第一梯队区间；
但若要做“显著优于大多数库”的对外结论，仍建议补一组同机同负载的 A/B 基准（至少与 2-3 个常见方案同口径对比）。
