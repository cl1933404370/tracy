// ChromeExportExample.cpp - Example for TracyChromeExport offline tracer
// Build:   mkdir build && cd build && cmake .. && make
// Run:     ./ChromeExportExample
// Output goes to stdout; pipe to file then import:
//   ./ChromeExportExample > trace.json
//   tracy-import-chrome trace.json trace.tracy
//   tracy-profiler trace.tracy

#include <tracy/TracyHcomm.hpp>
#include <cstdio>
#include <cmath>
#include <thread>
#include <cstdlib>

// ── Simulated workloads ─────────────────────────────────────────────────────

void BusyWork( int iterations )
{
    ChromeZoneScoped;
    volatile double sum = 0;
    for( int i = 0; i < iterations; i++ )
        sum += std::sin( static_cast<double>(i) );
}

void ParseData()
{
    ChromeZoneScoped;
    BusyWork( 50000 );
}

void CompressData()
{
    ChromeZoneScoped;
    BusyWork( 80000 );
}

void ProcessFrame( int frameId )
{
    ChromeZoneNamed( "ProcessFrame" );

    ParseData();
    CompressData();

    ChromePlot( "fps", 60.0 - frameId * 0.5 );
    ChromePlot( "memory_bytes", 1024 * 1024 * (frameId + 1) );

    ChromeFrameMark;
}

void WorkerThread( int id )
{
    char name[32];
    snprintf( name, sizeof(name), "Worker_%d", id );
    ChromeSetThreadName( name );

    for( int i = 0; i < 5; i++ )
    {
        ChromeZoneNamed( "WorkerTask" );
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
    for( int i = 0; i < 10; i++ )
        ProcessFrame( i );
    std::thread t1( WorkerThread, 0 );
    std::thread t2( WorkerThread, 1 );
    std::thread t3( WorkerThread, 2 );
    t1.join();
    t2.join();
    t3.join();
    ChromeTraceDump();
    return 0;
}
