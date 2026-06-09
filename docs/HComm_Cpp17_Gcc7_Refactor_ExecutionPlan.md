# HComm Trace C++17 (GCC7 Compatible) Refactor Execution Plan

## 1. Goal

在不破坏现有稳定性的前提下，评估并落地 HComm trace 路径的 C++17 语法重构，验证是否有可观收益。

目标分为两层：

1. 工程层目标：保证 GCC 7 可编译、现有 WSL 预设可过、NoSendPerf 功能测试可过。
2. 性能层目标：使用 NoSendPerf 对 zone 和 export 做 A/B 对比，确认收益或至少无回退。

## 2. Scope And Constraints

### 2.1 In Scope

1. HComm trace 关键路径：
   1. public/client/TracyLiteAll.cpp
   2. public/client/TracyLitePerfetto.cpp
   3. tracy_hcom/TracyHcom.cpp
   4. tracy_hcom/TracyHcomBundle.cmake
2. NoSendPerf 验证路径：
   1. examples/NoSendPerf/TracyNoSendPerfCase.cpp
   2. examples/NoSendPerf/TracyNoSendPerfRunner.cpp

### 2.2 Out Of Scope

1. 行为语义变更（事件内容、导出协议、chunk 语义）
2. 大规模算法改写（例如重写导出协议栈）
3. 破坏性 API 变更

### 2.3 Compatibility Requirements

1. 必须兼容 GCC 7 + C++17。
2. 对于已保持 C++14 的路径，不做无必要升级。
3. 允许按翻译单元单独启用 C++17（当前仓内已有先例）。

## 3. GCC7-Compatible C++17 Subset

优先使用以下语法特性（GCC7 兼容较稳）：

1. if constexpr
2. structured bindings
3. inline variables（慎用，尽量局部静态替代）
4. constexpr 增强
5. std::string_view（只在明确安全的只读路径使用）
6. std::optional（仅在非热路径，避免不必要对象语义开销）

建议避免或谨慎使用：

1. filesystem（GCC7 常涉及额外链接和兼容问题）
2. charconv（实现完整性在 GCC7 阶段不稳定）
3. 复杂模板元编程重构（收益不确定，风险高）

## 4. Refactor Strategy

采用双阶段策略，保证可回滚和收益可归因。

### 4.1 Stage A: Mechanical C++17 Cleanup (Low Risk)

目标：仅做语法级重构，不改逻辑。

候选动作：

1. 局部推导优化：
   1. 用 structured bindings 简化 tuple/pair 访问。
   2. 用 if constexpr 消除编译期分支重复。
2. 只读字符串路径：
   1. 在不引入生命周期风险前提下评估 string_view。
3. 明确常量表达式：
   1. 将可 constexpr 的辅助函数和常量提升。

验收标准：

1. 所有行为测试与基线一致。
2. 性能无显著回退（zone、export 均在噪声范围内）。

### 4.2 Stage B: Hot-Path Friendly C++17 Restructure (Medium Risk)

目标：在热路径用更清晰的 C++17 表达，尝试微幅性能收益。

候选动作：

1. TracyLitePerfetto.cpp 序列化辅助路径的分支整理。
2. TracyLiteAll.cpp 中可静态分派的辅助逻辑改写。
3. 保持内存布局和数据结构不变，避免 ABI 风险。

验收标准：

1. 功能一致。
2. build/test 全绿。
3. NoSendPerf 指标出现稳定正向或至少无回退。

## 5. Build Matrix And Validation Gate

执行端必须跑以下 8 套 WSL 预设（configure + build）：

1. wsl2-hcomm-debug
2. wsl2-hcomm-release
3. wsl2-all-hcomm-debug
4. wsl2-all-hcomm-release
5. wsl2-client-debug
6. wsl2-client-release
7. wsl2-all-debug
8. wsl2-all-release

然后跑 NoSendPerf 核心测试：

1. TracyNoSendPerf.ZoneBeginEnd
2. TracyNoSendPerf.ExportSplit
3. TracyNoSendPerf.ChunkSelfTest
4. TracyNoSendPerf.ReconstructPrefixedE2E
5. TracyHcomCore.ZoneBeginEnd
6. TracyHcom.ZoneBeginEnd
7. TracyHcom.ExportSplit
8. TracyHcom.ChunkSelfTest

## 6. Benchmark Protocol (NoSendPerf)

### 6.1 Baseline Collection

每个候选分支修改前，执行以下采样：

1. zone 连续 20 次
2. export 连续 20 次
3. chunk 自测 1 次（功能性）

建议命令（Linux/WSL）：

```bash
cd /root/git/tracy

bin=out/wsl2-all-hcomm-release/examples/NoSendPerf/TracyNoSendPerfRunner

for mode in zone export; do
  sum=0
  count=0
  for i in $(seq 1 20); do
    line="$($bin $mode | tail -1)"
    if [[ "$mode" == "zone" ]]; then
      v=$(echo "$line" | awk -F'ns_per_pair=' '{print $2}' | awk '{print $1}')
    else
      v=$(echo "$line" | awk -F'total_elapsed_us=' '{print $2}' | awk '{print $1}')
    fi
    sum=$(awk -v s="$sum" -v x="$v" 'BEGIN{printf "%.10f", s+x}')
    count=$((count+1))
  done
  avg=$(awk -v s="$sum" -v c="$count" 'BEGIN{printf "%.2f", s/c}')
  echo "$mode avg=$avg"
done

$bin chunk
```

### 6.2 Post-Change Collection

修改后同口径重跑，禁止修改迭代次数和模式。

### 6.3 Reporting Template

每次提交都附同格式对比：

1. commit id
2. zone avg (before/after, delta)
3. export avg (before/after, delta)
4. chunk/self-test 是否通过
5. 8 WSL build matrix 是否全绿

## 7. Execution Plan For The Other Agent

执行端按以下顺序做，不要跳步：

1. 建立分支：cpp17-hcomm-trace-exp
2. 跑 baseline 并记录。
3. Stage A 机械重构，单次小提交（每次改动 1 到 2 文件）。
4. 每次提交后跑最小验证：
   1. wsl2-all-hcomm-release configure/build
   2. NoSendPerf zone/export/chunk
5. Stage A 完成后跑 8 套 WSL 全量矩阵。
6. 若 Stage A 无回退，再进入 Stage B。
7. Stage B 每次提交必须附 benchmark 结果。

## 8. Review Checklist (我方复审清单)

我方对执行端提交按以下清单复审：

1. 语义一致性
   1. 事件类型、时间戳、导出字段、chunk 格式无变化。
2. GCC7 兼容性
   1. 未使用 GCC7 风险较高的 C++17 特性。
3. 热路径风险
   1. 无额外动态分配。
   2. 无多余虚调用。
   3. 无明显分支膨胀。
4. 可维护性
   1. 仅必要重构，无风格噪音。
5. 验证完整性
   1. 8 套 WSL 编译结果齐全。
   2. NoSendPerf 对比数据齐全。

## 9. Rollback Policy

满足任一条件即回滚到上一步：

1. 任一 WSL 预设 build fail
2. NoSendPerf 任一功能测试 fail
3. zone 或 export 出现稳定回退（超出噪声范围）

建议噪声阈值：

1. zone: 3% 以内视作噪声
2. export: 5% 以内视作噪声

超阈值需给出原因解释或回滚。

## 10. Suggested First Patch Set

优先从这两个文件开始（收益潜力和风险平衡较好）：

1. public/client/TracyLitePerfetto.cpp
2. public/client/TracyLiteAll.cpp

第一批只做语法级清理，不做数据结构改动。

---

如果执行端按本文档推进并回传每轮数据，我方可以低 token 成本快速做差异复审、定位回退原因并给出下一轮改动建议。

## 11. Kickoff Prompt For Executor

把下面整段原样发给执行端即可开工：

你现在开始执行 HComm trace 的 C++17 重构实验，严格按 docs/HComm_Cpp17_Gcc7_Refactor_ExecutionPlan.md。

必须遵守：

1. 仅做 GCC7 兼容的 C++17 语法重构，不改行为语义。
2. 第一阶段只改以下两个文件：
   1. public/client/TracyLitePerfetto.cpp
   2. public/client/TracyLiteAll.cpp
3. 每次提交只做小改动，提交后立即验证。
4. 每轮都要回传：
   1. 改动文件和关键 diff
   2. NoSendPerf 对比数据（zone/export/chunk）
   3. WSL 8 套 preset 的 configure+build 结果

执行顺序：

1. 建分支 cpp17-hcomm-trace-exp
2. 跑 baseline（20 次 zone + 20 次 export + 1 次 chunk）
3. 做 Stage A 机械重构并提交第 1 轮
4. 跑最小验证（wsl2-all-hcomm-release + NoSendPerf）
5. 通过后继续第 2 轮
6. Stage A 结束后跑 8 套 WSL 全量矩阵

输出格式必须固定：

1. Commit: <hash>
2. Zone avg: before -> after (delta%)
3. Export avg: before -> after (delta%)
4. Chunk: PASS/FAIL
5. WSL matrix: 8/8 PASS or 列出失败项
6. 风险说明: 3 行以内
