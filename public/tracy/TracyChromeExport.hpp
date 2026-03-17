#ifndef __TRACY_CHROME_EXPORT_HPP__
#define __TRACY_CHROME_EXPORT_HPP__

// TracyChromeExport - Lightweight offline tracer that outputs Chrome JSON format
// Reuses Tracy's thread-ID primitive; uses thread-local buffers for lock-free recording.
//
// Usage:
//   #include "tracy/TracyChromeExport.hpp"
//
//   void MyFunction() {
//       ChromeZoneScoped;            // automatic function name
//   }
//
//   int main() {
//       // Set callback to receive each JSON event line (print, log, write file, etc.)
//       ChromeSetOutputCallback([](const char* line) { printf("%s\n", line); });
//       ChromeSetThreadName("MainThread");
//       MyFunction();
//       // Flush buffered events through callback (call after threads joined)
//       ChromeFlushToCallback();
//   }

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ── Reuse Tracy's thread-ID primitive ───────────────────────────────────────
#include "../common/TracySystem.hpp"

// ── Platform timing ─────────────────────────────────────────────────────────
// We intentionally do NOT use tracy::Profiler::GetTime() because on x86 it
// returns RDTSC ticks (not nanoseconds).  Calibrating those ticks requires
// the full Profiler infrastructure and m_timerMul, which is only available
// when TRACY_ENABLE is set and the profiler is connected.
// Instead we use the same monotonic-clock fallback path Tracy itself uses
// on the !TRACY_HW_TIMER / TRACY_TIMER_FALLBACK branch.

#ifdef __linux__
#  include <time.h>
#elif defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach/mach_time.h>
#endif

#if !defined(__linux__) && !defined(_WIN32) && !defined(__APPLE__)
#  include <chrono>
#endif

namespace chrome_export {

// ── Timing (nanoseconds, monotonic) ─────────────────────────────────────────

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
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch() ).count();
#endif
}

// ── Event storage ───────────────────────────────────────────────────────────

struct Event
{
    enum Type : uint8_t { ZoneBegin, ZoneEnd, FrameMark, PlotValue };

    Type     type;
    uint32_t tid;
    int64_t  ts_ns;       // timestamp in nanoseconds, relative to epoch
    const char* name;     // function/zone name (string literal, not owned)
    const char* file;     // __FILE__ (string literal)
    uint32_t    line;     // __LINE__
    double      value;    // PlotValue payload
};

// ── Per-thread event buffer ─────────────────────────────────────────────────
// Each thread writes into its own buffer with zero synchronization.
// A mutex is taken only ONCE per thread lifetime (on first access) to
// register the buffer with the central tracer.

struct ThreadBuffer
{
    std::vector<Event> events;
    std::deque<std::string> owned_strings;  // stable storage for dynamic names
    uint32_t    tid;
    std::string name;

    ThreadBuffer() : tid( 0 ) { events.reserve( 64 * 1024 ); }

    // Intern a string: copies into stable storage, returns pointer that
    // remains valid for the lifetime of this ThreadBuffer.
    const char* Intern( const char* s )
    {
        if( !s ) return nullptr;
        owned_strings.emplace_back( s );
        return owned_strings.back().c_str();
    }
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

    // Returns the calling thread's buffer.  Lock-free after the first call.
    ThreadBuffer* GetThreadBuffer()
    {
        static thread_local ThreadBuffer* s_buf = nullptr;
        if( !s_buf )
        {
            auto buf = std::make_unique<ThreadBuffer>();
            buf->tid = tracy::GetThreadHandle();
            s_buf = buf.get();
            std::lock_guard<std::mutex> lock( m_registryMutex );
            m_buffers.push_back( std::move( buf ) );
        }
        return s_buf;
    }

    void RecordBegin( const char* name, const char* file, uint32_t line )
    {
        auto* buf = GetThreadBuffer();
        Event e;
        e.type  = Event::ZoneBegin;
        e.tid   = buf->tid;
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name );
        e.file  = file;   // __FILE__ is always a string literal
        e.line  = line;
        e.value = 0;
        buf->events.push_back( e );
    }

    void RecordEnd()
    {
        auto* buf = GetThreadBuffer();
        Event e;
        e.type  = Event::ZoneEnd;
        e.tid   = buf->tid;
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = nullptr;
        e.file  = nullptr;
        e.line  = 0;
        e.value = 0;
        buf->events.push_back( e );
    }

    void RecordFrame( const char* name = nullptr )
    {
        auto* buf = GetThreadBuffer();
        Event e;
        e.type  = Event::FrameMark;
        e.tid   = buf->tid;
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name ? name : "frame" );
        e.file  = nullptr;
        e.line  = 0;
        e.value = 0;
        buf->events.push_back( e );
    }

    void RecordPlot( const char* name, double value )
    {
        auto* buf = GetThreadBuffer();
        Event e;
        e.type  = Event::PlotValue;
        e.tid   = buf->tid;
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name );
        e.file  = nullptr;
        e.line  = 0;
        e.value = value;
        buf->events.push_back( e );
    }

    void SetThreadName( const char* name )
    {
        auto* buf = GetThreadBuffer();
        buf->name = name;
    }

    // Format a single event as Chrome JSON.  Returns the number of chars written.
    // Buffer must be at least 512 bytes.
    static int FormatEvent( const Event& e, char* buf, size_t bufSize )
    {
        switch( e.type )
        {
        case Event::ZoneBegin:
            if( e.file && e.line > 0 )
                return snprintf( buf, bufSize,
                    "{\"ph\":\"B\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\",\"loc\":\"%s:%u\"}",
                    e.tid, (long long)( e.ts_ns / 1000 ), (long long)( e.ts_ns % 1000 ),
                    e.name ? e.name : "unknown", e.file, e.line );
            else
                return snprintf( buf, bufSize,
                    "{\"ph\":\"B\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\"}",
                    e.tid, (long long)( e.ts_ns / 1000 ), (long long)( e.ts_ns % 1000 ),
                    e.name ? e.name : "unknown" );
        case Event::ZoneEnd:
            return snprintf( buf, bufSize,
                "{\"ph\":\"E\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld}",
                e.tid, (long long)( e.ts_ns / 1000 ), (long long)( e.ts_ns % 1000 ) );
        case Event::FrameMark:
            return snprintf( buf, bufSize,
                "{\"ph\":\"i\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\",\"s\":\"g\"}",
                e.tid, (long long)( e.ts_ns / 1000 ), (long long)( e.ts_ns % 1000 ), e.name );
        case Event::PlotValue:
            return snprintf( buf, bufSize,
                "{\"ph\":\"C\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"args\":{\"%s\":%.6f}}",
                e.tid, (long long)( e.ts_ns / 1000 ), (long long)( e.ts_ns % 1000 ),
                e.name, e.value );
        }
        return 0;
    }

    static int FormatThreadName( uint32_t tid, const char* name, char* buf, size_t bufSize )
    {
        return snprintf( buf, bufSize,
            "{\"ph\":\"M\",\"pid\":1,\"tid\":%u,\"name\":\"thread_name\",\"args\":{\"name\":\"%s\"}}",
            tid, name );
    }

    // Set a callback to receive each Chrome JSON event line.
    // Callback signature: void(const char* json_line)
    typedef void (*OutputCallback)( const char* );

    void SetOutputCallback( OutputCallback cb )
    {
        m_outputCb = cb;
    }

    // Flush all buffered events through the output callback, one line per call.
    // Call after all worker threads have joined.
    void FlushToCallback()
    {
        if( !m_outputCb ) return;
        char buf[512];

        for( auto& tb : m_buffers )
        {
            if( !tb->name.empty() )
            {
                FormatThreadName( tb->tid, tb->name.c_str(), buf, sizeof( buf ) );
                m_outputCb( buf );
            }
        }
        for( auto& tb : m_buffers )
        {
            for( auto& e : tb->events )
            {
                FormatEvent( e, buf, sizeof( buf ) );
                m_outputCb( buf );
            }
        }
    }

    // Call after all worker threads have joined (same as Save).
    void Clear()
    {
        for( auto& buf : m_buffers )
        {
            buf->events.clear();
            buf->owned_strings.clear();
            buf->name.clear();
        }
    }

    // Call after all worker threads have joined (same as Save).
    size_t EventCount()
    {
        size_t count = 0;
        for( auto& buf : m_buffers ) count += buf->events.size();
        return count;
    }

private:
    ChromeTracer() : m_epochNs( GetTimeNs() ) {}

    ~ChromeTracer()
    {
        if( m_outputCb ) FlushToCallback();
    }

    int64_t    m_epochNs;          // epoch: all timestamps relative to this
    std::mutex m_registryMutex;    // protects m_buffers (taken once per thread)
    std::vector<std::unique_ptr<ThreadBuffer>> m_buffers;
    OutputCallback m_outputCb = nullptr;
};

// ── RAII Zone Guard ─────────────────────────────────────────────────────────

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

    ChromeScopedZone( const ChromeScopedZone& ) = delete;
    ChromeScopedZone& operator=( const ChromeScopedZone& ) = delete;
};

} // namespace chrome_export

// ── User-facing macros ──────────────────────────────────────────────────────

#define ChromeConcat2(a, b) a ## b
#define ChromeConcat(a, b) ChromeConcat2(a, b)

#define ChromeZoneScoped \
    chrome_export::ChromeScopedZone ChromeConcat(__chrome_zone_, __LINE__) \
        ( __FUNCTION__, __FILE__, __LINE__ )

#define ChromeZoneNamed(name) \
    chrome_export::ChromeScopedZone ChromeConcat(__chrome_zone_, __LINE__) \
        ( name, __FILE__, __LINE__ )

#define ChromeFrameMark \
    chrome_export::ChromeTracer::Instance().RecordFrame( nullptr )

#define ChromeFrameMarkNamed(name) \
    chrome_export::ChromeTracer::Instance().RecordFrame( name )

#define ChromePlot(name, val) \
    chrome_export::ChromeTracer::Instance().RecordPlot( name, (double)(val) )

#define ChromeSetThreadName(name) \
    chrome_export::ChromeTracer::Instance().SetThreadName( name )

#define ChromeSetOutputCallback(cb) \
    chrome_export::ChromeTracer::Instance().SetOutputCallback( cb )

#define ChromeFlushToCallback() \
    chrome_export::ChromeTracer::Instance().FlushToCallback()

#endif // __TRACY_CHROME_EXPORT_HPP__
