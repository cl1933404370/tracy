//
// Created by c60039780 on 2026/3/24.
//

#ifndef TRACY_TRACY_CHROME_EXPORT_H
#define TRACY_TRACY_CHROME_EXPORT_H
#include <deque>
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

#ifdef __linux__
#  include <time.h>
#  include <unistd.h>
#  include <sys/syscall.h>
#elif defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach/mach_time.h>
#  include <pthread.h>
#else
#  /* Try to include unistd for other POSIX-like systems to get getpid() */
#  include <unistd.h>
#endif

#if defined( _MSVC_LANG )
#  define TRACY_CPP_VERSION _MSVC_LANG
#elif defined( __cplusplus )
#  define TRACY_CPP_VERSION __cplusplus
#else
#  define TRACY_CPP_VERSION 199711L
#endif
// 检查 C++ 版本
#if TRACY_CPP_VERSION >= 202002L
#  define TRACY_CPP20
#elif TRACY_CPP_VERSION >= 201703L
#  define TRACY_CPP17
#elif TRACY_CPP_VERSION >= 201402L
#  define TRACY_CPP14
#else
#  define TRACY_CPP11
#endif

// constexpr 文件名提取
namespace tracy_internal
{
#if TRACY_CPP_VERSION >= 201402L
    // C++14+ 支持循环 constexpr
constexpr const char* tracy_filename( const char* path )
{
    const char* last = path;
    for( const char* p = path; *p; ++p )
    {
        if( *p == '/' || *p == '\\' ) last = p + 1;
    }
    return last;
}
#else
    // C++11 条件: 仅允许单一 return 语句（不能循环）, 使用递归实现
constexpr const char* str_end( const char* str )
{
    return *str ? str_end( str + 1 ) : str;
}

constexpr bool str_slant( const char* str )
{
    return *str ? ( *str == '/' ? true : str_slant( str + 1 ) ) : false;
}

constexpr const char* r_slant( const char* str )
{
    return *str == '/' ? ( str + 1 ) : r_slant( str - 1 );
}

constexpr const char* tracy_filename( const char* path )
{
    return str_slant( path ) ? r_slant( str_end( path ) ) : path;
}
#endif
}

#if TRACY_CPP_VERSION >= 202002L
#  include <source_location>
#  define TracyFile std::source_location::current().file_name()
#else
#  define TracyFile ::tracy_internal::tracy_filename( __FILE__ )
#endif

#if !defined(__linux__) && !defined(_WIN32) && !defined(__APPLE__)
#  include <chrono>
#endif
#include <common/TracyForceInline.hpp>

/* C++14 compatibility: provide a nodiscard macro if compiling pre-C++17 */
#ifndef TRACY_NODISCARD
#  if __cplusplus >= 201703L
#    define TRACY_NODISCARD [[nodiscard]]
#  else
#    define TRACY_NODISCARD
#  endif
#endif

namespace chrome_export {

// ── Timing (nanoseconds, monotonic) ─────────────────────────────────────────

static tracy_force_inline int64_t GetTimeNs()
{
#if defined(__linux__) && defined(CLOCK_MONOTONIC_RAW)
    timespec ts{};
    clock_gettime( CLOCK_MONOTONIC_RAW, &ts );
    return ts.tv_sec * 1000000000ll + ts.tv_nsec;
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
private:
    std::vector<Event> events;
    std::deque<std::string> owned_strings;  // stable storage for dynamic names
    uint32_t    tid;
    std::string name;

public:
    ThreadBuffer();
    ThreadBuffer( const ThreadBuffer& ) = default;
    ThreadBuffer( ThreadBuffer&& ) noexcept = default;
    ThreadBuffer& operator=( const ThreadBuffer& ) = default;
    ThreadBuffer& operator=( ThreadBuffer&& ) = default;

    void SetTid( const uint32_t t ) { tid = t; }
    TRACY_NODISCARD uint32_t GetTid() const { return tid; }
    void SetName( const char* n ) { name = n ? n : ""; }
    TRACY_NODISCARD const std::string& GetName() const { return name; }
    std::deque<std::string>& GetOwnedStrings() { return owned_strings; }
    // Intern a string: copies into stable storage, returns pointer that
    // remains valid for the lifetime of this ThreadBuffer.
    const char* Intern( const char* s );
    // Provide access to events if needed
    std::vector<Event>& GetEvents() { return events; }
    TRACY_NODISCARD const std::vector<Event>& GetEvents() const { return events; }
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
        Event e{};
        e.type  = Event::ZoneBeginEvent;
        e.tid = buf->GetTid();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name );
        e.file  = file;   // __FILE__ is always a string literal
        e.line  = line;
        e.value = 0;
        buf->GetEvents().push_back(e);
    }

    void RecordEnd()
    {
        auto* buf = GetThreadBuffer();
        Event e{};
        e.type  = Event::ZoneEndEvent;
        e.tid = buf->GetTid();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = nullptr;
        e.file  = nullptr;
        e.line  = 0;
        e.value = 0;
        buf->GetEvents().push_back(e);
    }

    void RecordFrame( const char* name = nullptr )
    {
        auto* buf = GetThreadBuffer();
        Event e{};
        e.type  = Event::FrameMarkEvent;
        e.tid = buf->GetTid();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name ? name : "frame" );
        e.file  = nullptr;
        e.line  = 0;
        e.value = 0;
        buf->GetEvents().push_back(e);
    }

    void RecordPlot( const char* name, const double value )
    {
        auto* buf = GetThreadBuffer();
        Event e{};
        e.type  = Event::PlotValueEvent;
        e.tid = buf->GetTid();
        e.ts_ns = GetTimeNs() - m_epochNs;
        e.name  = buf->Intern( name );
        e.file  = nullptr;
        e.line  = 0;
        e.value = value;
        buf->GetEvents().push_back(e);
    }

    void SetThreadName( const char* name );

    // Format a single event as Chrome JSON.  Returns the number of chars written.
    // Buffer must be at least 512 bytes.
    static int FormatEvent( const Event& e, char* buf, size_t bufSize );

    static int FormatThreadName( uint32_t tid, const char* name, char* buf, size_t bufSize );

    // Set a callback to receive each Chrome JSON event line.
    // Callback signature: void(const char* JSON_line)
    using OutputCallback = void ( * )( const char*);

    void SetOutputCallback( OutputCallback cb );

    // Flush all buffered events through the output callback, one line per call.
    // Call after all worker threads have joined.
    void FlushToCallback() const;

    // Call after all worker threads have joined (same as Safe).
    void Clear() const;

    // Call after all worker threads have joined (same as Safe).
    TRACY_NODISCARD size_t EventCount() const;

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

inline uint64_t GetLogPid()
{
#ifdef _WIN32
    static const uint64_t pid = static_cast<uint64_t>(GetCurrentProcessId());
    return pid;
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    static const auto pid = static_cast<uint64_t>(getpid());
    return pid;
#else
    // Unknown platform: fallback to zero
    return 0;
#endif
}

inline uint64_t GetLogTid()
{
#ifdef __linux__
    thread_local const auto tid = static_cast<uint64_t>(syscall( __NR_gettid ));
    return tid;
#elif defined(_WIN32)
    // DWORD -> uint64_t
    return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__APPLE__)
    // pthread_threadid_np provides a uint64_t thread id on macOS
    uint64_t tid = 0;
    (void)pthread_threadid_np( NULL, &tid );
    return tid;
#else
    // Portable fallback: hash of std::thread::id; stable per thread for process lifetime
    thread_local const uint64_t tid = static_cast<uint64_t>(std::hash<std::thread::id>()( std::this_thread::get_id() ));
    return tid;
#endif
}

struct ChromeInit
{
    static std::once_flag g_dumpLogOnce;
    static ChromeInit& Instance( const std::string& ChromeSetThreadName = "", ChromeTracer::OutputCallback cb = nullptr );
    ~ChromeInit();
    static void DumpLog();
};
} // namespace chrome_export
#endif //TRACY_TRACY_CHROME_EXPORT_H
