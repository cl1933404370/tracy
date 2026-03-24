//
// Created by c60039780 on 2026/3/24.
//

#ifndef TRACY_TRACY_CHROME_EXPORT_H
#define TRACY_TRACY_CHROME_EXPORT_H
#include <deque>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>
#include <cstddef>
#include <string>

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
#include <common/TracyForceInline.hpp>

namespace chrome_export {

// ── Timing (nanoseconds, monotonic) ─────────────────────────────────────────

static tracy_force_inline int64_t GetTimeNs()
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
    return static_cast<int64_t>( static_cast<double>( now.QuadPart ) / freq.QuadPart * 1e9 );
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
    enum Type : uint8_t { ZoneBeginEvent, ZoneEndEvent, FrameMarkEvent, PlotValueEvent };
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
    ThreadBuffer();
    // Intern a string: copies into stable storage, returns pointer that
    // remains valid for the lifetime of this ThreadBuffer.
    const char* Intern( const char* s );
};

// ── Core tracer (singleton) ─────────────────────────────────────────────────

class ChromeTracer
{
public:
    static ChromeTracer& Instance();

    // Returns the calling thread's buffer.  Lock-free after the first call.
    ThreadBuffer* GetThreadBuffer();

    void RecordBegin( const char* name, const char* file, const uint32_t line )
    {
        auto* buf = GetThreadBuffer();
        Event e;
        e.type  = Event::ZoneBeginEvent;
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
        e.type  = Event::ZoneEndEvent;
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
        e.type  = Event::FrameMarkEvent;
        e.tid   = buf->tid;
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name ? name : "frame" );
        e.file  = nullptr;
        e.line  = 0;
        e.value = 0;
        buf->events.push_back( e );
    }

    void RecordPlot( const char* name, const double value )
    {
        auto* buf = GetThreadBuffer();
        Event e;
        e.type  = Event::PlotValueEvent;
        e.tid   = buf->tid;
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name );
        e.file  = nullptr;
        e.line  = 0;
        e.value = value;
        buf->events.push_back( e );
    }

    void SetThreadName( const char* name );

    // Format a single event as Chrome JSON.  Returns the number of chars written.
    // Buffer must be at least 512 bytes.
    static int FormatEvent( const Event& e, char* buf, size_t bufSize );

    static int FormatThreadName( uint32_t tid, const char* name, char* buf, size_t bufSize );

    // Set a callback to receive each Chrome JSON event line.
    // Callback signature: void(const char* JSON_line)
    typedef void (*OutputCallback)( const char* );

    void SetOutputCallback( OutputCallback cb );

    // Flush all buffered events through the output callback, one line per call.
    // Call after all worker threads have joined.
    void FlushToCallback() const;

    // Call after all worker threads have joined (same as Safe).
    void Clear() const;

    // Call after all worker threads have joined (same as Safe).
    size_t EventCount() const;

private:
    ChromeTracer() : m_epochNs( GetTimeNs() ) {}

    ~ChromeTracer();

    int64_t    m_epochNs;          // epoch: all timestamps relative to this
    std::mutex m_registryMutex;    // protects m_buffers (taken once per thread)
    std::vector<std::unique_ptr<ThreadBuffer>> m_buffers;
    OutputCallback m_outputCb = nullptr;
};

// ── RAII Zone Guard ─────────────────────────────────────────────────────────

class ChromeScopedZone
{
public:
    ChromeScopedZone( const char* name, const char* file, uint32_t line );
    ~ChromeScopedZone();
    ChromeScopedZone( const ChromeScopedZone& ) = delete;
    ChromeScopedZone& operator=( const ChromeScopedZone& ) = delete;
};

} // namespace chrome_export
#endif //TRACY_TRACY_CHROME_EXPORT_H
