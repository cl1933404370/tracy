// test_chrome_export.cpp - End-to-end test for TracyChromeExport
// Compile: g++ -std=c++17 -O2 -o test_chrome_export test_chrome_export.cpp -lpthread
// Run:     ./test_chrome_export
// Convert: ./import/build/tracy-import-chrome trace.json trace.tracy
// View:    DISPLAY=:0 GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 ./profiler/build/tracy-profiler trace.tracy

#include "public/tracy/TracyChromeExport.hpp"
#include <cstdio>
#include <cmath>
#include <thread>

// ── Simulated workloads ─────────────────────────────────────────────────────

void BusyWork( int iterations )
{
    ChromeZoneScoped;
    volatile double sum = 0;
    for( int i = 0; i < iterations; i++ )
        sum += std::sin( (double)i );
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

    // Record a plot value: simulated FPS
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
    ChromeSetThreadName( "MainThread" );

    printf( "Running test...\n" );

    // Main thread: 10 frames
    for( int i = 0; i < 10; i++ )
        ProcessFrame( i );

    // Spawn 3 worker threads
    std::thread t1( WorkerThread, 0 );
    std::thread t2( WorkerThread, 1 );
    std::thread t3( WorkerThread, 2 );
    t1.join();
    t2.join();
    t3.join();

    // Save
    const char* outPath = "trace.json";
    if( ChromeTraceSave( outPath ) )
        printf( "Saved %zu events to %s\n",
            chrome_export::ChromeTracer::Instance().EventCount(), outPath );
    else
        printf( "Failed to save!\n" );

    return 0;
}
