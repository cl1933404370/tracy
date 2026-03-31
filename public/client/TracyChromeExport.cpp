//
// Created by c60039780 on 2026/3/24.
//

#include "TracyChromeExport.hpp"
#include "../common/TracyQueue.hpp"
#include "tracy_rpmalloc.hpp"
#include <string>
#ifdef _WIN32
#  include <direct.h>
#endif
#include <algorithm>
#include <cstdio>
#if defined( __linux__ ) || defined( __ANDROID__ )
#  include <syslog.h>
#endif
#include <fstream>
#if defined( TRACY_SAVE_NO_SEND ) && defined( TRACY_ENABLE )

tracy_dump::ThreadBuffer::ThreadBuffer() noexcept
{
    // Initial reservation using rpmalloc. Use alignment equal to alignof(max_align_t).
    // saved_capacity is in bytes. We choose to reserve space for approximately
    // 64K QueueItem-sized entries here (so multiply by sizeof(QueueItem)). This
    // gives a larger initial buffer suitable when caller writes QueueItem records
    // into the blob. If you prefer a byte-based initial value, change this constant.
    constexpr size_t init = 64ULL * 1024ULL * sizeof( tracy::QueueItem ); // bytes
    // Allocate initial buffer (init bytes).
    saved_blob = static_cast<char*>( tracy::rpaligned_alloc( alignof( std::max_align_t ), init ) );
    if( saved_blob ) saved_capacity = init;
}

tracy_dump::ThreadBuffer::~ThreadBuffer() noexcept
{
    if( saved_blob ) tracy::rpfree( saved_blob );
    saved_blob = nullptr;
    saved_capacity = saved_size = 0;
}

void tracy_dump::ThreadBuffer::AddSavedData( const void* data, const size_t len )
{
    if( !data || len == 0 ) return;
    // length prefix 4 bytes + payload
    const size_t need = sizeof( uint32_t ) + len;
    if( saved_capacity < saved_size + need )
    {
        size_t nc = saved_capacity ? saved_capacity : 64ULL * 1024ULL;
        while( nc < saved_size + need ) nc *= 2;
        if( !ReserveSavedBlobBytes( nc ) ) return; // still cannot grow
    }
    const auto l = static_cast<uint32_t>( len );
    memcpy( saved_blob + saved_size, &l, sizeof( l ) );
    memcpy( saved_blob + saved_size + sizeof( l ), data, len );
    saved_size += need;
}

void tracy_dump::ThreadBuffer::AddSavedLine( const char* line )
{
    if( !line ) return;
    AddSavedData( line, strlen( line ) );
}

void tracy_dump::ThreadBuffer::ClearSavedBlob()
{
    saved_size = 0;
}

bool tracy_dump::ThreadBuffer::ReserveSavedBlobBytes( const size_t bytes )
{
    if( bytes <= saved_capacity ) return true;

    // Log expansion request: print previous and requested capacities. This helps
    // debugging when the buffer grows. Keep the logging low-volume (stderr/syslog)
    // since this should only happen infrequently.
    {
        char msg[256];
        const int n = snprintf( msg, sizeof( msg ), "Tracy: ReserveSavedBlob expanding from %zu to %zu bytes\n", saved_capacity, bytes );
        if( n > 0 )
        {
#  ifdef _WIN32
            OutputDebugStringA( msg );
#  endif
#  if defined( __linux__ ) || defined( __ANDROID__ )
            syslog( LOG_INFO, "%s", msg );
#  endif
            std::fprintf( stderr, "%s", msg );
        }
    }

    // allocate new buffer with requested capacity (aligned)
    const auto nb = static_cast<char*>( tracy::rpaligned_alloc( alignof( std::max_align_t ), bytes ) );
    if( !nb )
    {
        // Allocation failed: report and keep old buffer intact.
        char msg[256];
        const int n = snprintf( msg, sizeof( msg ), "Tracy: ReserveSavedBlobBytes allocation failed for %zu bytes\n", bytes );
        if( n > 0 )
        {
#  ifdef _WIN32
            OutputDebugStringA( msg );
#  endif
#  if defined( __linux__ ) || defined( __ANDROID__ )
            syslog( LOG_ERR, "%s", msg );
#  endif
            std::fprintf( stderr, "%s", msg );
        }
        return false; // keep old buffer
    }
     // allocation failed -> keep old buffer
    if( saved_blob && saved_size > 0 ) memcpy( nb, saved_blob, saved_size );
    // free old buffer
    if( saved_blob ) tracy::rpfree( saved_blob );
    saved_blob = nb;
    saved_capacity = bytes;
    return true;
}

tracy_dump::ChromeTracer& tracy_dump::ChromeTracer::Instance()
{
    static ChromeTracer s_instance;
    return s_instance;
}

tracy_dump::ThreadBuffer* tracy_dump::ChromeTracer::GetThreadBuffer()
{
    thread_local ThreadBuffer* s_buf = nullptr;
    if( !s_buf )
    {
        auto buf = std::make_unique<ThreadBuffer>();
#  if TRACY_CPP_VERSION >= 201703L
        std::scoped_lock const lock( m_registryMutex );
#  else
        std::lock_guard<std::mutex> lock( m_registryMutex );
#  endif
        m_buffers.push_back( std::move( buf ) );
        s_buf = m_buffers.back().get();
    }
    return s_buf;
}

void tracy_dump::ChromeTracer::SetOutputCallback( const OutputCallback cb )
{
    m_outputCb = cb;
}

void tracy_dump::ChromeTracer::FlushToCallback() const
{
    if( !m_outputCb ) return;
    // Lock registry while iterating buffers to avoid concurrent mutation.
#  if TRACY_CPP_VERSION >= 201703L
    std::scoped_lock const lock( m_registryMutex );
#  else
    std::lock_guard<std::mutex> const lk( m_registryMutex );
#  endif
    for( auto& tb : m_buffers )
    {
        const char* data = tb->GetSavedBlobPtr();
        const size_t size = tb->GetSavedBlobSize();
        if( !data || size == 0 ) continue;
        // Iterate frames stored as [uint32_t len][bytes...] and call the
        // output callback with each frame's pointer+size so the callback can
        // write binary data (e.g. to a .Tracy file).
        IterateSavedFramesFromPtr( data, size, [cb = m_outputCb]( const char* f, const size_t s ) {
            if( cb ) cb( f, s );
        } );
    }
}

void tracy_dump::ChromeTracer::Clear() const
{
    for( auto& buf : m_buffers )
    {
        buf->ClearSavedBlob();
    }
}

size_t tracy_dump::ChromeTracer::EventCount() const
{
    size_t count = 0;
    for( auto& buf : m_buffers ) count += buf->GetSavedBlobSize() / sizeof( tracy::QueueItem );
    return count;
}

tracy_dump::ChromeTracer::~ChromeTracer() noexcept
{
    // Destructor must not throw. Perform only minimal, non-allocating cleanup.
    try
    {
#  if TRACY_CPP_VERSION >= 201703L
        std::scoped_lock const lock( m_registryMutex );
#  else
        std::lock_guard<std::mutex> lk( m_registryMutex );
#  endif
        m_buffers.clear();
        m_outputCb = nullptr;
    }
    catch( const std::bad_alloc& )
    {
        std::fprintf( stderr, "Warning: std::bad_alloc in ~ChromeTracer ignored\n" );
    }
    catch( const std::exception& ex )
    {
        std::fprintf( stderr, "Warning: exception in ~ChromeTracer ignored: %s\n", ex.what() );
    }
    catch( ... )
    {
        std::fputs( "Warning: unknown exception in ~ChromeTracer ignored\n", stderr );
    }
}

void tracy_dump::SaveProfilerData( const void* data, const size_t len )
{
    auto* buf = ChromeTracer::Instance().GetThreadBuffer();
    if( buf ) buf->AddSavedData( data, len );
}

void tracy_dump::IterateSavedFramesFromPtr( const char* data, const size_t size, const std::function<void( const char*, size_t )>& cb )
{
    size_t pos = 0;
    while( pos + sizeof( uint32_t ) <= size )
    {
        uint32_t len;
        memcpy( &len, data + pos, sizeof( uint32_t ) );
        pos += sizeof( uint32_t );
        if( pos + static_cast<size_t>( len ) > size ) break; // truncated
        cb( data + pos, len );
        pos += static_cast<size_t>( len );
    }
}

void tracy_dump::IterateSavedFrames( const std::vector<char>& blob, const std::function<void( const char*, size_t )>& cb )
{
    // Reuse pointer-based implementation
    if( blob.empty() ) return;
    IterateSavedFramesFromPtr( blob.data(), blob.size(), cb );
}

namespace
{
std::string GetAscendProcessLogPath()
{
    std::string path;
#  ifdef __linux__
#    ifndef CCL_KERNEL_AICPU
    const char* env = std::getenv( "ASCEND_PROCESS_LOG_PATH" );
    if( env && env[0] != '\0' )
    {
        path = env;
    }
    else
    {
        path = "/root/ascend/log";
    }
#    else
    path = "/var/log/npu/slog";
#    endif
    if( !path.empty() && path.back() != '/' )
    {
        path.push_back( '/' );
    }
#  endif
    // Ensure directory exists (recursive). Portable implementation for C++14.
    auto CreateDirectories = []( const std::string& dir ) -> bool {
        if( dir.empty() ) return false;
        std::string p = dir;
        std::replace( p.begin(), p.end(), '\\', '/' );
        std::string cur;
        size_t i = 0;
        if( p.size() >= 2 && p[1] == ':' )
        {
            // Windows drive letter
            cur = p.substr( 0, 2 );
            if( p.size() > 2 && p[2] == '/' ) cur += '/';
            i = cur.size();
        }
        else if( !p.empty() && p[0] == '/' )
        {
            cur = "/";
            i = 1;
        }

        while( i < p.size() )
        {
            if( p[i] == '/' )
            {
                ++i;
                continue;
            }
            const size_t j = p.find( '/', i );
            const std::string token = j == std::string::npos ? p.substr( i ) : p.substr( i, j - i );
            if( !cur.empty() && cur.back() != '/' ) cur += '/';
            cur += token;

            // skip creating root or bare drive like "C:"
            if( cur != "/" && ( cur.size() != 2 || cur[1] != ':' ) )
            {
#  ifdef _WIN32
                const int r = _mkdir( cur.c_str() );
#  else
                int r = mkdir( cur.c_str(), 0755 );
#  endif
                if( r != 0 && errno != EEXIST ) return false;
            }

            if( j == std::string::npos ) break;
            i = j + 1;
        }
        return true;
    };

    CreateDirectories( path );
    return path;
}

std::string GetProcessLogFileName()
{
    // Use .Tracy extension for binary profiler dumps. Compute pid in a
    // platform-portable way rather than calling into other tracy symbols.
#  ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#  else
    const auto pid = static_cast<unsigned long>( getpid() );
#  endif
    return GetAscendProcessLogPath() + std::to_string( pid ) + ".Tracy";
}

std::string g_file_name; // compute lazily to avoid throwing during static init
std::mutex g_log_lock;

void LogC( const char* data, const size_t size )
{
#  if TRACY_CPP_VERSION >= 201703L
    std::scoped_lock const lock( g_log_lock );
#  else
    std::lock_guard<std::mutex> const lk( g_log_lock );
#  endif
    if( g_file_name.empty() )
    {
        try
        {
            g_file_name = GetProcessLogFileName();
        }
        catch( ... )
        {
            perror( "GetProcessLogFileName" );
            return;
        }
    }
    // Open in binary append mode to write raw frames.
    std::ofstream ofs( g_file_name, std::ios::app | std::ios::binary );
    if( !ofs )
    {
        perror( g_file_name.c_str() );
    }
    else
    {
        // Write exact bytes provided by caller (no NUL-termination assumed).
        ofs.write( data, static_cast<std::streamsize>( size ) );
        ofs.close();
    }
}
} // namespace

tracy_dump::ChromeInit& tracy_dump::ChromeInit::Instance( const ChromeTracer::OutputCallback cb )
{
    static ChromeInit s_instance;
    if( cb )
    {
        ChromeTracer::Instance().SetOutputCallback( cb );
    }
    else
    {
        ChromeTracer::Instance().SetOutputCallback( LogC );
    }
    return s_instance;
}

std::once_flag tracy_dump::ChromeInit::g_dumpLogOnce;

void tracy_dump::ChromeInit::DumpLog()
{
    std::call_once( g_dumpLogOnce, [] {
        if( ChromeTracer::Instance().EventCount() == 0 )
        {
            return;
        }
        // Emit chrome JSON as before
        ChromeTracer::Instance().FlushToCallback();
        ChromeTracer::Instance().Clear();
        ChromeTracer::Instance().SetOutputCallback( nullptr );
    } );
}

tracy_dump::ChromeInit::~ChromeInit() noexcept
{
    try
    {
        DumpLog();
    }
    catch( const std::exception& ex )
    {
        std::fprintf( stderr, "Warning: exception in ~ChromeInit ignored: %s\n", ex.what() );
    }
    catch( ... )
    {
        std::fputs( "Warning: unknown exception in ~ChromeInit ignored\n", stderr );
    }
}

// Portable approach: global object whose ctor runs before main()
namespace
{
struct ChromeAutoInit
{
    ChromeAutoInit() noexcept
    {
        try
        {
            tracy_dump::ChromeInit::Instance( nullptr );
        }
        catch( const std::exception& ex )
        {
            std::fprintf( stderr, "Warning: exception in ChromeAutoInit ctor ignored: %s\n", ex.what() );
        }
        catch( ... )
        {
            std::fputs( "Warning: unknown exception in ChromeAutoInit ctor ignored\n", stderr );
        }
    }
};

ChromeAutoInit g_chromeAutoInit;
}
#endif // TRACY_SAVE_NO_SEND && TRACY_ENABLE
