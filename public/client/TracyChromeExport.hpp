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

#ifdef _MSVC_LANG
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

#if defined( TRACY_SAVE_NO_SEND ) && defined( TRACY_ENABLE )
namespace tracy_dump
{
struct ThreadBuffer
{
private:
    // single contiguous buffer allocated from rpmalloc (rpaligned_alloc). The
    // layout is length-prefixed frames: [uint32_t len][bytes]...
    char* saved_blob = nullptr;
    size_t saved_size = 0;
    size_t saved_capacity = 0;

public:
    ThreadBuffer() noexcept;
    ~ThreadBuffer() noexcept;
    ThreadBuffer( const ThreadBuffer& ) = delete;
    ThreadBuffer( ThreadBuffer&& ) noexcept = delete;
    ThreadBuffer& operator=( const ThreadBuffer& ) = delete;
    ThreadBuffer& operator=( ThreadBuffer&& ) = delete;

    // Append raw binary data (no locking required - buffer is per-thread)
    void AddSavedData( const void* data, size_t len );
    // Append a NUL-terminated text line (helper)
    void AddSavedLine( const char* line );
    // Intern a string into owned_strings and return stable pointer
    const char* Intern( const char* s );

    // Accessors for saved blob (pointer+size)
    const char* GetSavedBlobPtr() const { return saved_blob; }
    size_t GetSavedBlobSize() const { return saved_size; }
    void ClearSavedBlob();
    // Ensure capacity in bytes. Returns true on success, false if allocation failed.
    // saved_capacity and saved_size are measured in bytes.
    bool ReserveSavedBlobBytes( size_t bytes );
};

// ── Core tracer (singleton) ─────────────────────────────────────────────────

// Main dump/tracer singleton. Collects per-thread trace frames and
// can flush them either as Chrome events or as a ProfileChunk for flamegraphs.
class ChromeTracer
{
public:
    static ChromeTracer& Instance();

    // Returns the calling thread's buffer.  Lock-free after the first call.
    ThreadBuffer* GetThreadBuffer();
    // Output callback receives a pointer to a frame and its size. Using a
    // pointer+size allows emitting binary .Tracy files instead of text JSON.
    using OutputCallback = void ( * )( const char*, size_t );
    void SetOutputCallback( OutputCallback cb );
    // Flush all buffered events through the output callback, one line per call.
    // Call after all worker threads have joined.
    void FlushToCallback() const;
    // Also flush a ProfileChunk (ph:"P") representation built from B/E events
    // so Chrome can display a flamegraph. Emits a single JSON event containing
    // samples and stackFrames. Requires an output callback to be set.
    void FlushProfileToCallback() const;
    // Call after all worker threads have joined (same as Safe).
    void Clear() const;
    // Call after all worker threads have joined (same as Safe).
    TRACY_NODISCARD size_t EventCount() const;

private:
    ChromeTracer()
        : m_outputCb( nullptr )
    {
    }
    ~ChromeTracer() noexcept;
    mutable std::mutex m_registryMutex;
    std::vector<std::unique_ptr<ThreadBuffer>> m_buffers;
    OutputCallback m_outputCb;
};

struct ChromeInit
{
    static std::once_flag g_dumpLogOnce;
    static ChromeInit& Instance( ChromeTracer::OutputCallback cb = nullptr );
    ~ChromeInit() noexcept;
    static void DumpLog();
};

// Save raw profiler frame data into thread-local storage. Data is stored as
// [uint32_t len][bytes...]. This function uses a thread_local ThreadBuffer
// so callers do not need to manage per-thread storage.
void SaveProfilerData( const void* data, size_t len );

// Implementations are in the .cpp file. Also provide an overload that accepts
// a raw pointer/size so callers can iterate frames stored in ThreadBuffer
// without copying into a std::vector.
void IterateSavedFramesFromPtr( const char* data, size_t size, const std::function<void( const char*, size_t )>& cb );

// Original vector-based helper (kept for compatibility; implemented in .cpp)
void IterateSavedFrames( const std::vector<char>& blob, const std::function<void( const char*, size_t )>& cb );
} // namespace tracy_dump
#else
// When TRACY_SAVE_NO_SEND or TRACY_ENABLE is not defined, provide no declarations
// to avoid any static initialization or extra code generation.
#endif // defined(TRACY_SAVE_NO_SEND) && defined(TRACY_ENABLE)
#endif //TRACY_TRACY_CHROME_EXPORT_H
