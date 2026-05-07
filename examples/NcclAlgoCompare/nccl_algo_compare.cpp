// nccl_algo_compare.cpp
// 模拟 NCCL/HCCL 集合通信算法，用 ChromeExport 对比 Ring vs Tree AllReduce 的耗时
//
// 编译:  make
// 运行:  ./nccl_algo_compare
// 产出:  trace.json  →  可直接拖进 chrome://tracing 或
//        tracy-import-chrome trace.json trace.tracy && tracy-profiler trace.tracy
//
// 后续在真实 MPICH / NCCL 源码中：
//   1. #include "tracy/TracyHcomm.hpp"
//   2. 在关键函数入口加 ChromeZoneScoped / ChromeZoneNamed("xxx")
//   3. 链接 TracySystem.cpp，Makefile 加一行 SRCS 即可

#include "tracy/TracyHcomm.hpp"
#include "client/TracyLitePerfetto.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

namespace
{

// ═══════════════════════════════════════════════════════════════════════════
// 模拟参数
// ═══════════════════════════════════════════════════════════════════════════

constexpr int NUM_RANKS = 8; // 模拟 8 个 GPU（线程模拟）
constexpr int DATA_SIZE = 1 << 20; // 1M floats per rank
constexpr int LATENCY_US = 5; // 模拟每步网络延迟 5μs
constexpr int BW_FLOATS = 200000; // 模拟带宽: 每步能传 200K floats
constexpr int NUM_ITERS = 3; // 跑 3 轮取对比

// ── 模拟网络传输延迟 ──────────────────────────────────────────────────────
void SimulateTransfer( const int chunk_size )
{
    ZoneScopedN( "Transfer" ); // NOLINT(*-const-correctness)
    // 传输耗时 = 延迟 + 数据量/带宽
    const int us = LATENCY_US + chunk_size / ( BW_FLOATS / 1000 );
    std::this_thread::sleep_for( std::chrono::microseconds( us ) );
}

// ── 模拟 Reduce 计算 ─────────────────────────────────────────────────────
void SimulateReduce( const int count )
{
    ZoneScopedN( "Reduce" ); // NOLINT(*-const-correctness)
    volatile float sum = 0;
    for( int i = 0; i < count; i++ )
        sum += 1.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Ring AllReduce
// ═══════════════════════════════════════════════════════════════════════════
// 分 2*(N-1) 步：N-1 步 ReduceScatter + N-1 步 AllGather
// 每步传输 data_size / N 个 float

void RingAllReduce_Rank( const int rank, const int total_ranks, const int data_size )
{
    (void)rank;
    ZoneScopedN( "RingAllReduce" ); // NOLINT(*-const-correctness)

    const int chunk = data_size / total_ranks;
    const int steps = total_ranks - 1;

    // Phase 1: ReduceScatter

    {
        ZoneScopedN( "Ring::ReduceScatter" ); // NOLINT(*-const-correctness)
        for( int s = 0; s < steps; s++ )
        {
            SimulateTransfer( chunk );
            SimulateReduce( chunk );
        }
    }

    // Phase 2: AllGather
    {
        ZoneScopedN( "Ring::AllGather" ); // NOLINT(*-const-correctness)
        for( int s = 0; s < steps; s++ )
        {
            SimulateTransfer( chunk );
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Tree (Recursive Halving-Doubling) AllReduce
// ═══════════════════════════════════════════════════════════════════════════
// 分 2*log2(N) 步：log2(N) 步 ReduceScatter + log2(N) 步 AllGather
// 每步传输 data_size / 2 个 float（但步数少）

void TreeAllReduce_Rank( const int rank, const int total_ranks, const int data_size )
{
    (void)rank;
    ZoneScopedN( "TreeAllReduce" ); // NOLINT(*-const-correctness)

    int log2n = 0;
    for( int n = total_ranks; n > 1; n >>= 1 ) log2n++;

    // Phase 1: Recursive Halving (Reduce)
    {
        ZoneScopedN( "Tree::RecursiveHalving" ); // NOLINT(*-const-correctness)
        int chunk = data_size;
        for( int d = 0; d < log2n; d++ )
        {
            chunk /= 2;
            SimulateTransfer( chunk );
            SimulateReduce( chunk );
        }
    }

    // Phase 2: Recursive Doubling (Broadcast)
    {
        ZoneScopedN( "Tree::RecursiveDoubling" ); // NOLINT(*-const-correctness)
        int chunk = data_size / total_ranks;
        for( int d = 0; d < log2n; d++ )
        {
            SimulateTransfer( chunk );
            chunk *= 2;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 运行一轮：所有 rank 并行执行，等全部结束 → 一轮完成
// ═══════════════════════════════════════════════════════════════════════════

using AlgoFunc = std::function<void( int rank, int total, int size )>;

void RunAllRanks( const char* algo_name, const AlgoFunc& fn, const int iter )
{
    char zone_name[64];
    snprintf( zone_name, sizeof( zone_name ), "%s_iter%d", algo_name, iter );
    // Use ZoneTransientN for a runtime zone name; wrap with
    // SuppressVarShadowWarning to match ZoneScoped behavior without
    // modifying Tracy.hpp.
     // ZoneTransientN uses the native Tracy profiler which isn't started under
     // TRACY_SAVE_NO_SEND + MANUAL_LIFETIME.  Use a compile-time name instead.
     ZoneScopedN( "RunAllRanks" ); // NOLINT(*-const-correctness)

    std::vector<std::thread> threads;
    threads.reserve( NUM_RANKS );

    for( int r = 0; r < NUM_RANKS; r++ )
    {
        threads.emplace_back( [=] {
            // 每个 rank 一个线程，设置线程名
            char name[32];
            snprintf( name, sizeof( name ), "%s_Rank%d", algo_name, r );
            // tracy::SetThreadName requires the native profiler to be running.
            // Under TRACY_SAVE_NO_SEND + MANUAL_LIFETIME the profiler is never
            // started, so skip the native call (OS-level name is still set).
            // tracy::SetThreadName( name );

            fn( r, NUM_RANKS, DATA_SIZE );

            // 标一个 Plot 模拟 GPU 利用率
            TracyPlot( "gpu_util", 70.0 + r * 3.0 );
        } );
    }

    for( auto& t : threads ) t.join();
}
}

int main()
{
    try
    {
#ifndef _WIN32
        setenv( "ASCEND_PROCESS_LOG_PATH", "/tmp/ascend_log", 1 );
#else
        _putenv_s( "ASCEND_PROCESS_LOG_PATH", "C:\\tmp\\ascend_log" );
#endif

#ifdef TRACYLITE_PERFETTO
        // 通过环境变量选择 counter 轨道模式：
        //   TRACYLITE_COUNTER_TRACK_MODE=thread   -> PerThread
        //   TRACYLITE_COUNTER_TRACK_MODE=process  -> PerProcess (默认)
        const char* modeEnv = std::getenv( "TRACYLITE_COUNTER_TRACK_MODE" );
        const bool perThread = modeEnv &&
            ( std::strcmp( modeEnv, "thread" ) == 0 ||
              std::strcmp( modeEnv, "per_thread" ) == 0 ||
              std::strcmp( modeEnv, "perthread" ) == 0 );
        tracylite::PerfettoNativeExporter::SetCounterTrackMode(
            perThread
                ? tracylite::PerfettoNativeExporter::CounterTrackMode::PerThread
                : tracylite::PerfettoNativeExporter::CounterTrackMode::PerProcess );
#endif

        printf( "=== NCCL/HCCL AllReduce Algorithm Comparison ===\n" );
        printf( "Ranks: %d, DataSize: %d floats, Iters: %d\n\n", NUM_RANKS, DATA_SIZE, NUM_ITERS );

    // ── Ring AllReduce ──
        for( int i = 0; i < NUM_ITERS; i++ )
        {
            fprintf( stderr, "Ring iter %d: FrameMark\n", i );
            FrameMarkNamed( "Ring_Iteration" );
            fprintf( stderr, "Ring iter %d: RunAllRanks\n", i );
            RunAllRanks( "Ring", RingAllReduce_Rank, i );
            fprintf( stderr, "Ring iter %d: done\n", i );
        }

    // ── Tree AllReduce ──
        for( int i = 0; i < NUM_ITERS; i++ )
        {
            FrameMarkNamed( "Tree_Iteration" );
            RunAllRanks( "Tree", TreeAllReduce_Rank, i );
        }

    // ── 输出 trace.json ──
#ifdef TRACY_SAVE_NO_SEND
        fprintf( stderr, "Buffered: %zu, Dropped: %zu\n",
                 tracylite::Collector::GetBufferSize(),
                 tracylite::Collector::GetDroppedCount() );
#endif
        ChromeTraceDump();
        fprintf( stderr, "Export done\n" );
    }
    catch( ... )
    {
        std::printf("An unexpected error occurred.\n");
        return 1;
    }
    return 0;
}
