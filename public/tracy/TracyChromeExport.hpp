#ifndef __TRACY_CHROME_EXPORT_HPP__
#define __TRACY_CHROME_EXPORT_HPP__

// TracyChromeExport - Lightweight offline tracer that outputs Chrome JSON format
// Reuses Tracy's timing and threading primitives for maximum compatibility.
//
// Usage:
//   #include "tracy/TracyChromeExport.hpp"
//
//   void MyFunction() {
//       ChromeZoneScoped;            // automatic function name
//       // ... work ...
//   }
//
//   void MyFunction2() {
//       ChromeZoneNamed("CustomName");  // custom zone name
//       // ... work ...
//   }
//
//   int main() {
//       ChromeTracer::Instance().SetThreadName("MainThread");
//       MyFunction();
//       MyFunction2();
//       ChromeTracer::Instance().Save("trace.json");
//   }
//
// Then convert and view:
//   ./tracy-import-chrome trace.json output.tracy
//   ./tracy-profiler output.tracy

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

// ── Reuse Tracy's platform-specific primitives ──────────────────────────────

#ifdef __linux__
#  include <time.h>
#  include <unistd.h>
#  include <sys/syscall.h>
#elif defined(_WIN32)
#  include <windows.h>
#  include <intrin.h>
#elif defined(__APPLE__)
#  include <mach/mach_time.h>
#  include <pthread.h>
#endif

namespace chrome_export {

// ── Timing ──────────────────────────────────────────────────────────────────
// Matches Tracy's Profiler::GetTime() fallback path.
// Returns nanoseconds from a monotonic clock.

static inline int64_t GetTimeNs()
{
#if defined(__linux__) && defined(CLOCK_MONOTONIC_RAW)
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC_RAW, &ts );
    return int64_t( ts.tv_sec ) * 1000000000ll + int64_t( ts.tv_nsec );
#elif defined(_WIN32)
    static LARGE_INTEGER freq = {};
    if( freq.QuadPart == 0 ) QueryPerformanceFrequency( &freq );
    LARGE_INTEGER now;
    QueryPerformanceCounter( &now );
    return (int64_t)( (double)now.QuadPart / freq.QuadPart * 1e9 );
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb = {};
    if( tb.denom == 0 ) mach_timebase_info( &tb );
    return (int64_t)( mach_absolute_time() * tb.numer / tb.denom );
#else
    #include <chrono>
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch() ).count();
#endif
}

// Returns microseconds (Chrome JSON uses μs as timestamp unit)
static inline double GetTimeUs()
{
    return GetTimeNs() / 1000.0;
}

// ── Thread ID ───────────────────────────────────────────────────────────────
// Matches Tracy's detail::GetThreadHandleImpl()

static inline uint32_t GetThreadId()
{
#if defined(_WIN32)
    return (uint32_t)GetCurrentThreadId();
#elif defined(__APPLE__)
    uint64_t id;
    pthread_threadid_np( pthread_self(), &id );
    return (uint32_t)id;
#elif defined(__linux__)
    return (uint32_t)syscall( SYS_gettid );
#else
    return 0;
#endif
}

// ── Event storage ───────────────────────────────────────────────────────────

struct Event
{
    enum Type : uint8_t { ZoneBegin, ZoneEnd, FrameMark, PlotValue };

    Type     type;
    uint32_t tid;
    int64_t  ts_ns;       // timestamp in nanoseconds, relative to epoch
    // ZoneBegin fields
    const char* name;     // function/zone name (string literal, not owned)
    const char* file;     // __FILE__ (string literal)
    uint32_t    line;     // __LINE__
    // PlotValue fields
    double      value;
};

// ── Core tracer (singleton) ─────────────────────────────────────────────────

class ChromeTracer
{
public:
    static ChromeTracer& Instance()
    {
        static ChromeTracer s_instance;
        return s_instance;
    }

    void RecordBegin( const char* name, const char* file, uint32_t line )
    {
        Event e;
        e.type = Event::ZoneBegin;
        e.tid  = GetThreadId();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name = name;
        e.file = file;
        e.line = line;
        e.value = 0;
        Push( e );
    }

    void RecordEnd()
    {
        Event e;
        e.type = Event::ZoneEnd;
        e.tid  = GetThreadId();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name = nullptr;
        e.file = nullptr;
        e.line = 0;
        e.value = 0;
        Push( e );
    }

    void RecordFrame( const char* name = nullptr )
    {
        Event e;
        e.type = Event::FrameMark;
        e.tid  = GetThreadId();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name = name ? name : "frame";
        e.file = nullptr;
        e.line = 0;
        e.value = 0;
        Push( e );
    }

    void RecordPlot( const char* name, double value )
    {
        Event e;
        e.type = Event::PlotValue;
        e.tid  = GetThreadId();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name = name;
        e.file = nullptr;
        e.line = 0;
        e.value = value;
        Push( e );
    }

    void SetThreadName( const char* name )
    {
        uint32_t tid = GetThreadId();
        std::lock_guard<std::mutex> lock( m_mutex );
        m_threadNames[tid] = std::string( name );
    }

    // Save all recorded events as Chrome JSON.
    // Call this once, typically at program exit.
    bool Save( const char* path )
    {
        FILE* f = fopen( path, "w" );
        if( !f ) return false;

        fprintf( f, "{\"traceEvents\":[\n" );

        bool first = true;

        // Thread name metadata events
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            for( auto& kv : m_threadNames )
            {
                if( !first ) fprintf( f, ",\n" );
                first = false;
                fprintf( f, "{\"ph\":\"M\",\"pid\":1,\"tid\":%u,\"name\":\"thread_name\",\"args\":{\"name\":\"%s\"}}",
                    kv.first, kv.second.c_str() );
            }
        }

        // All trace events
        std::lock_guard<std::mutex> lock( m_mutex );
        for( auto& e : m_events )
        {
            if( !first ) fprintf( f, ",\n" );
            first = false;

            switch( e.type )
            {
            case Event::ZoneBegin:
                fprintf( f, "{\"ph\":\"B\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\"",
                    e.tid, (long long)(e.ts_ns / 1000), (long long)(e.ts_ns % 1000), e.name ? e.name : "unknown" );
                if( e.file && e.line > 0 )
                    fprintf( f, ",\"loc\":\"%s:%u\"", e.file, e.line );
                fprintf( f, "}" );
                break;

            case Event::ZoneEnd:
                fprintf( f, "{\"ph\":\"E\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld}",
                    e.tid, (long long)(e.ts_ns / 1000), (long long)(e.ts_ns % 1000) );
                break;

            case Event::FrameMark:
                fprintf( f, "{\"ph\":\"i\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\",\"s\":\"g\"}",
                    e.tid, (long long)(e.ts_ns / 1000), (long long)(e.ts_ns % 1000), e.name );
                break;

            case Event::PlotValue:
                fprintf( f, "{\"ph\":\"C\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"args\":{\"%s\":%.6f}}",
                    e.tid, (long long)(e.ts_ns / 1000), (long long)(e.ts_ns % 1000), e.name, e.value );
                break;
            }
        }

        fprintf( f, "\n]}\n" );
        fclose( f );
        return true;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_events.clear();
        m_threadNames.clear();
    }

    size_t EventCount()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_events.size();
    }

private:
    ChromeTracer() : m_epochNs( GetTimeNs() )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_events.reserve( 1024 * 1024 ); // pre alloc 1M entries, avoid runtime realloc
    }

    void Push( const Event& e )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_events.push_back( e );
    }

    int64_t m_epochNs;   // epoch: all timestamps are relative to this
    std::mutex m_mutex;
    std::vector<Event> m_events;
    std::unordered_map<uint32_t, std::string> m_threadNames;
};

// ── RAII Zone Guard ─────────────────────────────────────────────────────────
// Mirrors Tracy's ScopedZone: constructor records Begin, destructor records End.

class ChromeScopedZone
{
public:
    ChromeScopedZone( const char* name, const char* file, uint32_t line )
    {
        ChromeTracer::Instance().RecordBegin( name, file, line );
    }
    ~ChromeScopedZone()
    {
        ChromeTracer::Instance().RecordEnd();
    }

    // Non-copyable
    ChromeScopedZone( const ChromeScopedZone& ) = delete;
    ChromeScopedZone& operator=( const ChromeScopedZone& ) = delete;
};

} // namespace chrome_export

// ── User-facing macros ──────────────────────────────────────────────────────
// Pattern identical to Tracy's ZoneScoped / ZoneNamed macros.

#define ChromeConcat2(a, b) a ## b
#define ChromeConcat(a, b) ChromeConcat2(a, b)

// Automatic: uses __FUNCTION__ as zone name
#define ChromeZoneScoped \
    chrome_export::ChromeScopedZone ChromeConcat(__chrome_zone_, __LINE__) \
        ( __FUNCTION__, __FILE__, __LINE__ )

// Custom name
#define ChromeZoneNamed(name) \
    chrome_export::ChromeScopedZone ChromeConcat(__chrome_zone_, __LINE__) \
        ( name, __FILE__, __LINE__ )

// Frame mark (maps to Tracy's FrameMark)
#define ChromeFrameMark \
    chrome_export::ChromeTracer::Instance().RecordFrame( nullptr )

#define ChromeFrameMarkNamed(name) \
    chrome_export::ChromeTracer::Instance().RecordFrame( name )

// Plot value (maps to Tracy's TracyPlot)
#define ChromePlot(name, val) \
    chrome_export::ChromeTracer::Instance().RecordPlot( name, (double)(val) )

// Save to file
#define ChromeTraceSave(path) \
    chrome_export::ChromeTracer::Instance().Save( path )

// Set thread name
#define ChromeSetThreadName(name) \
    chrome_export::ChromeTracer::Instance().SetThreadName( name )

#endif // __TRACY_CHROME_EXPORT_HPP__
