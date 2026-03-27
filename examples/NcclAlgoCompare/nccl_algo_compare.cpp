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

#include <cstdio>
#include <cmath>
#include <thread>
#include <vector>
#include <chrono>
#include <functional>

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
    ChromeZoneNamed( "Transfer" );
    // 传输耗时 = 延迟 + 数据量/带宽
    const int us = LATENCY_US + chunk_size / ( BW_FLOATS / 1000 );
    std::this_thread::sleep_for( std::chrono::microseconds( us ) );
}

// ── 模拟 Reduce 计算 ─────────────────────────────────────────────────────
void SimulateReduce( const int count )
{
    ChromeZoneNamed( "Reduce" );
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
    ChromeZoneNamed( "RingAllReduce" );

    const int chunk = data_size / total_ranks;
    const int steps = total_ranks - 1;

    // Phase 1: ReduceScatter

    {
        ChromeZoneNamed( "Ring::ReduceScatter" );
        for( int s = 0; s < steps; s++ )
        {
            SimulateTransfer( chunk );
            SimulateReduce( chunk );
        }
    }

    // Phase 2: AllGather
    {
        ChromeZoneNamed( "Ring::AllGather" );
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
    ChromeZoneNamed( "TreeAllReduce" );

    int log2n = 0;
    for( int n = total_ranks; n > 1; n >>= 1 ) log2n++;

    // Phase 1: Recursive Halving (Reduce)
    {
        ChromeZoneNamed( "Tree::RecursiveHalving" );
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
        ChromeZoneNamed( "Tree::RecursiveDoubling" );
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
    // 主线程标记这一轮
    ChromeZoneNamed( zone_name );

    std::vector<std::thread> threads;
    threads.reserve( NUM_RANKS );

    for( int r = 0; r < NUM_RANKS; r++ )
    {
        threads.emplace_back( [=]() {
            // 每个 rank 一个线程，设置线程名
            char name[32];
            snprintf( name, sizeof( name ), "%s_Rank%d", algo_name, r );
            ChromeSetThreadName( name );

            fn( r, NUM_RANKS, DATA_SIZE );

            // 标一个 Plot 模拟 GPU 利用率
            ChromePlot( "gpu_util", 70.0 + r * 3.0 );
        } );
    }

    for( auto& t : threads ) t.join();
}
}

int main()
{
#ifndef _WIN32
    setenv( "ASCEND_PROCESS_LOG_PATH", "/tmp/ascend_log", 1 );
#else
    _putenv_s( "ASCEND_PROCESS_LOG_PATH", "C:\\tmp\\ascend_log" );
#endif
    printf( "=== NCCL/HCCL AllReduce Algorithm Comparison ===\n" );
    printf( "Ranks: %d, DataSize: %d floats, Iters: %d\n\n", NUM_RANKS, DATA_SIZE, NUM_ITERS );

    // ── Ring AllReduce ──
    for( int i = 0; i < NUM_ITERS; i++ )
    {
        ChromeFrameMarkNamed( "Ring_Iteration" );
        RunAllRanks( "Ring", RingAllReduce_Rank, i );
    }

    // ── Tree AllReduce ──
    for( int i = 0; i < NUM_ITERS; i++ )
    {
        ChromeFrameMarkNamed( "Tree_Iteration" );
        RunAllRanks( "Tree", TreeAllReduce_Rank, i );
    }

    // ── 输出 trace.json ──
    ChromeTraceDump();
    printf( "\nDone! Wrote trace.json (%zu events)\n",
            chrome_export::ChromeTracer::Instance().EventCount() );
    printf( "Open with:  chrome://tracing  or\n" );
    printf( "Convert:    tracy-import-chrome trace.json trace.tracy\n" );
    return 0;
}
