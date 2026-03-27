//
// Created by c60039780 on 2026/3/24.
//

#include "TracyChromeExport.hpp"
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include "../common/TracySystem.hpp"

chrome_export::ThreadBuffer::ThreadBuffer()
    : tid( 0 )
{
    events.reserve( 64 * 1024 );
}

const char* chrome_export::ThreadBuffer::Intern( const char* s )
{
    if(!s) return nullptr;
    owned_strings.emplace_back( s );
    return owned_strings.back().c_str();
}

chrome_export::ChromeTracer& chrome_export::ChromeTracer::Instance()
{
    static ChromeTracer s_instance;
    return s_instance;
}

chrome_export::ThreadBuffer* chrome_export::ChromeTracer::GetThreadBuffer()
{
    thread_local ThreadBuffer* s_buf = nullptr;
    if(!s_buf)
    {
        auto buf = std::make_unique<ThreadBuffer>();
        buf->SetTid( tracy::GetThreadHandle() );
        s_buf = buf.get();
        std::lock_guard<std::mutex> const lock( m_registryMutex );
        m_buffers.push_back( std::move( buf ) );
    }
    return s_buf;
}

void chrome_export::ChromeTracer::SetThreadName( const char* name )
{
    auto* buf = GetThreadBuffer();
    buf->SetName( name );
}

int chrome_export::ChromeTracer::FormatEvent( const Event& e, char* buf, const size_t bufSize )
{
    switch(e.type)
    {
    case Event::ZoneBeginEvent: {
        if(e.file && e.line > 0)
            return snprintf( buf, bufSize,
                             R"({"ph":"B","pid":1,"tid":%u,"ts":%lld.%03lld,"name":"%s","loc":"%s:%u"},)",
                             e.tid, static_cast<long long>(e.ts_ns / 1000), static_cast<long long>(e.ts_ns % 1000),
                             e.name ? e.name : "unknown", e.file, e.line );
        return snprintf( buf, bufSize,
                         R"({"ph":"B","pid":1,"tid":%u,"ts":%lld.%03lld,"name":"%s"},)",
                         e.tid, static_cast<long long>(e.ts_ns / 1000), static_cast<long long>(e.ts_ns % 1000),
                         e.name ? e.name : "unknown" );
    }
    case Event::ZoneEndEvent: return snprintf( buf, bufSize,
                                               R"({"ph":"E","pid":1,"tid":%u,"ts":%lld.%03lld},)",
                                               e.tid, static_cast<long long>(e.ts_ns / 1000), static_cast<long long>(e.ts_ns % 1000) );
    case Event::FrameMarkEvent: return snprintf( buf, bufSize,
                                                 R"({"ph":"i","pid":1,"tid":%u,"ts":%lld.%03lld,"name":"%s","s":"g"},)",
                                                 e.tid, static_cast<long long>(e.ts_ns / 1000), static_cast<long long>(e.ts_ns % 1000), e.name );
    case Event::PlotValueEvent: return snprintf( buf, bufSize,
                                                 R"({"ph":"C","pid":1,"tid":%u,"ts":%lld.%03lld,"args":{"%s":%.6f}},)",
                                                 e.tid, static_cast<long long>(e.ts_ns / 1000), static_cast<long long>(e.ts_ns % 1000),
                                                 e.name, e.value );
    }
    return 0;
}

int chrome_export::ChromeTracer::FormatThreadName( const uint32_t tid, const char* name, char* buf, const size_t bufSize )
{
    return snprintf( buf, bufSize,
                     R"({"ph":"M","pid":1,"tid":%u,"name":"thread_name","args":{"name":"%s"}},)",
                     tid, name );
}

void chrome_export::ChromeTracer::SetOutputCallback( const OutputCallback cb )
{
    m_outputCb = cb;
}

void chrome_export::ChromeTracer::FlushToCallback() const
{
    if(!m_outputCb) return;
    char buf[512];

    for(auto& tb : m_buffers)
    {
        if(!tb->GetName().empty())
        {
            FormatThreadName( tb->GetTid(), tb->GetName().c_str(), buf, sizeof( buf ) );
            m_outputCb( buf );
        }
    }
    for(auto& tb : m_buffers)
    {
        for(auto& e : tb->GetEvents())
        {
            FormatEvent( e, buf, sizeof( buf ) );
            m_outputCb( buf );
        }
    }
}

void chrome_export::ChromeTracer::Clear() const
{
    for(auto& buf : m_buffers)
    {
        buf->GetEvents().clear();
        buf->GetOwnedStrings().clear();
        buf->SetName( nullptr );
    }
}

size_t chrome_export::ChromeTracer::EventCount() const
{
    size_t count = 0;
    for(auto& buf : m_buffers) count += buf->GetEvents().size();
    return count;
}

chrome_export::ChromeTracer::~ChromeTracer()
{
    if(m_outputCb) FlushToCallback();
}

chrome_export::ChromeScopedZone::ChromeScopedZone( const char* name, const char* file, const uint32_t line )
{
    ChromeTracer::Instance().RecordBegin( name, file, line );
}

chrome_export::ChromeScopedZone::~ChromeScopedZone()
{
    ChromeTracer::Instance().RecordEnd();
}

namespace
{

std::string GetAscendProcessLogPath()
{
    std::string path;
#ifdef __linux__
#ifndef CCL_KERNEL_AICPU
    const char* env = std::getenv( "ASCEND_PROCESS_LOG_PATH" );
    if(env && env[0] != '\0')
    {
        path = env;
    }
    else
    {
        path = "/root/ascend/log";
    }
#else
    path = "/var/log/npu/slog";
#endif
    if(!path.empty() && path.back() != '/')
    {
        path.push_back( '/' );
    }
#endif
    mkdir( path.c_str(), 0755 );
    return path;
}

std::string GetProcessLogFileName()
{
    return GetAscendProcessLogPath() + std::to_string( chrome_export::GetLogPid() ) + "_hccl.json";
}

std::string g_fileName = GetProcessLogFileName();
std::mutex g_logLock;

void logC( const char* format )
{
    time_t loacl_time;
    char time_str[128];

    // 获取本地时间
    time( &loacl_time );
    strftime( time_str, sizeof( time_str ), "[%Y.%m.%d %X]", localtime( &loacl_time ) );

    // 写日志文件
    std::lock_guard<std::mutex> const lk( g_logLock );
    FILE* file_fp = fopen( g_fileName.c_str(), "a" );
    if(!file_fp)
    {
        perror( g_fileName.c_str() );
    }
    else
    {
        fprintf( file_fp, "%s\n", format );
        fclose( file_fp );
    }
}
} // namespace

chrome_export::ChromeInit& chrome_export::ChromeInit::Instance( const std::string& ChromeSetThreadName, const ChromeTracer::OutputCallback cb )
{
    static ChromeInit s_instance;
    if(cb)
    {
        ChromeTracer::Instance().SetOutputCallback( cb );
    }
    else
    {
        ChromeTracer::Instance().SetOutputCallback( logC );
    }
    if(!ChromeSetThreadName.empty())
    {
        ChromeTracer::Instance().SetThreadName( ChromeSetThreadName.c_str() );
    }
    else
    {
        ChromeTracer::Instance().SetThreadName( std::to_string( GetLogTid() ).c_str() );
    }
    return s_instance;
}

std::once_flag chrome_export::ChromeInit::g_dumpLogOnce;

void chrome_export::ChromeInit::DumpLog()
{
    std::call_once( g_dumpLogOnce, [] {
        logC( "[" );
        ChromeTracer::Instance().FlushToCallback();
        logC( "\n]\n" );
        ChromeTracer::Instance().Clear();
    } );
}

chrome_export::ChromeInit::~ChromeInit()
{
    DumpLog();
}

// Portable approach: global object whose ctor runs before main()
namespace
{
struct ChromeAutoInit
{
    ChromeAutoInit()
    {
        chrome_export::ChromeInit::Instance( "", nullptr );
    }
};

ChromeAutoInit g_chromeAutoInit;
}
