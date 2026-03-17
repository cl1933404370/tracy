// ChromeExportExample.cpp - Example for TracyChromeExport offline tracer
// Build:   mkdir build && cd build && cmake .. && make
// Run:     ./ChromeExportExample
// Output goes to stdout; pipe to file then import:
//   ./ChromeExportExample > trace.json
//   tracy-import-chrome trace.json trace.tracy
//   tracy-profiler trace.tracy

#include <tracy/TracyChromeExport.hpp>
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

// Example: write a complete Chrome JSON file via callback
static FILE* g_outFile = nullptr;
static bool  g_first = true;

void WriteEventLine( const char* json )
{
    if( !g_first ) fprintf( g_outFile, ",\n" );
    g_first = false;
    fputs( json, g_outFile );
}

// Global init: register callback and main thread name before main()
static struct ChromeInit {
    ChromeInit() {
        ChromeSetOutputCallback( WriteEventLine );
        ChromeSetThreadName( "MainThread" );
    }
} g_chromeInit;

int main()
{
    fprintf( stderr, "Running test...\n" );

    for( int i = 0; i < 10; i++ )
        ProcessFrame( i );

    std::thread t1( WorkerThread, 0 );
    std::thread t2( WorkerThread, 1 );
    std::thread t3( WorkerThread, 2 );
    t1.join();
    t2.join();
    t3.join();

    // Flush all events through callback → write to file
    g_outFile = fopen( "trace.json", "w" );
    fprintf( g_outFile, "{\"traceEvents\":[\n" );
    ChromeFlushToCallback();
    fprintf( g_outFile, "\n]}\n" );
    fclose( g_outFile );

    fprintf( stderr, "Saved %zu events to trace.json\n",
        chrome_export::ChromeTracer::Instance().EventCount() );

    return 0;
}
