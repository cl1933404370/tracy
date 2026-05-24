// ChromeExportExample.cpp - Example for TracyChromeExport offline tracer
// Build:   mkdir build && cd build && cmake .. && make
// Run:     ./ChromeExportExample
// Output goes to stdout; pipe to file then import:
//   ./ChromeExportExample > trace.json
//   tracy-import-chrome trace.json trace.tracy
//   tracy-profiler trace.tracy

#include <cstdio>
#include <cmath>
#include <thread>
#include <tracy/TracyHcomm.hpp>

// ── Simulated workloads ─────────────────────────────────────────────────────

void BusyWork( const int iterations )
{
    volatile double sum = 0;
    for( int i = 0; i < iterations; i++ )
        sum += std::sin( static_cast<double>(i) );
}

void ParseData()
{
    BusyWork( 50000 );
}

void CompressData()
{
    BusyWork( 80000 );
}

void ProcessFrame( const int frameId )
{
    // Test memory tracking
    auto* buf = new char[1024 * (frameId + 1)];
    ParseData();
    CompressData();
    delete[] buf;
    TracyPlot( "fps", 60.0 - frameId * 0.5 );
    FrameMark;
}

void WorkerThread( const int id )
{
    char name[32];
    snprintf( name, sizeof(name), "Worker_%d", id );
    tracy::SetThreadName( name );

    for( int i = 0; i < 5; i++ )
    {
        ZoneScopedN( "WorkerTask" ); // NOLINT(*-const-correctness)
        BusyWork( 30000 + id * 10000 );
    }
}

int main()
{
#ifndef _WIN32
    setenv( "ASCEND_PROCESS_LOG_PATH", "/tmp/ascend_log", 1 );
#else
    _putenv_s( "ASCEND_PROCESS_LOG_PATH", "C:\\tmp\\ascend_log" );
#endif
    fprintf( stderr, "Running test...\n" );
    for( int i = 0; i < 2; i++ )
        ProcessFrame( i );
    // std::thread t1( WorkerThread, 0 );
    // std::thread t2( WorkerThread, 1 );
    // std::thread t3( WorkerThread, 2 );
    // t1.join();
    // t2.join();
    // t3.join();
#ifdef TRACY_SAVE_NO_SEND
    fprintf( stderr, "Buffered events before export: %zu\n", tracylite::Collector::GetBufferSize() );
    const auto ok = tracylite::DumpManager::Instance().ExportNow();
    fprintf( stderr, "Export result: %s\n", ok ? "OK" : "FAILED" );
#else
    fprintf( stderr, "TRACY_SAVE_NO_SEND is OFF; skip offline dump path.\n" );
#endif
    return 0;
}
