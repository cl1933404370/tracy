//
// Created by c60039780 on 2026/3/24.
//

#include "TracyChromeExport.hpp"
#include "../common/TracySystem.hpp"
chrome_export::ThreadBuffer::ThreadBuffer()
    : tid( 0 )
{
    events.reserve( 64 * 1024 );
}
const char* chrome_export::ThreadBuffer::Intern( const char* s )
{
    if( !s ) return nullptr;
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
void chrome_export::ChromeTracer::SetThreadName( const char* name )
{
    auto* buf = GetThreadBuffer();
    buf->name = name;
}
int chrome_export::ChromeTracer::FormatEvent( const Event& e, char* buf, const size_t bufSize )
{
    switch( e.type )
    {
    case Event::ZoneBeginEvent: {
        if( e.file && e.line > 0 )
            return snprintf( buf, bufSize,
                             "{\"ph\":\"B\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\",\"loc\":\"%s:%u\"}",
                             e.tid, static_cast<long long>( e.ts_ns / 1000 ), static_cast<long long>( e.ts_ns % 1000 ),
                             e.name ? e.name : "unknown", e.file, e.line );
        return snprintf( buf, bufSize,
                         "{\"ph\":\"B\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\"}",
                         e.tid, static_cast<long long>( e.ts_ns / 1000 ), static_cast<long long>( e.ts_ns % 1000 ),
                         e.name ? e.name : "unknown" );
    }
    case Event::ZoneEndEvent:
        return snprintf( buf, bufSize,
                         "{\"ph\":\"E\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld}",
                         e.tid, static_cast<long long>( e.ts_ns / 1000 ), static_cast<long long>( e.ts_ns % 1000 ) );
    case Event::FrameMarkEvent:
        return snprintf( buf, bufSize,
                         "{\"ph\":\"i\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"name\":\"%s\",\"s\":\"g\"}",
                         e.tid, static_cast<long long>( e.ts_ns / 1000 ), static_cast<long long>( e.ts_ns % 1000 ), e.name );
    case Event::PlotValueEvent:
        return snprintf( buf, bufSize,
                         "{\"ph\":\"C\",\"pid\":1,\"tid\":%u,\"ts\":%lld.%03lld,\"args\":{\"%s\":%.6f}}",
                         e.tid, static_cast<long long>( e.ts_ns / 1000 ), static_cast<long long>( e.ts_ns % 1000 ),
                         e.name, e.value );
    }
    return 0;
}
int chrome_export::ChromeTracer::FormatThreadName( const uint32_t tid, const char* name, char* buf, const size_t bufSize )
{
    return snprintf( buf, bufSize,
                     "{\"ph\":\"M\",\"pid\":1,\"tid\":%u,\"name\":\"thread_name\",\"args\":{\"name\":\"%s\"}}",
                     tid, name );
}
void chrome_export::ChromeTracer::SetOutputCallback( const OutputCallback cb )
{
    m_outputCb = cb;
}
void chrome_export::ChromeTracer::FlushToCallback() const
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
void chrome_export::ChromeTracer::Clear() const
{
    for( auto& buf : m_buffers )
    {
        buf->events.clear();
        buf->owned_strings.clear();
        buf->name.clear();
    }
}
size_t chrome_export::ChromeTracer::EventCount() const
{
    size_t count = 0;
    for( auto& buf : m_buffers ) count += buf->events.size();
    return count;
}
chrome_export::ChromeTracer::~ChromeTracer()
{
    if( m_outputCb ) FlushToCallback();
}
chrome_export::ChromeScopedZone::ChromeScopedZone( const char* name, const char* file, const uint32_t line )
{
    ChromeTracer::Instance().RecordBegin( name, file, line );
}
chrome_export::ChromeScopedZone::~ChromeScopedZone()
{
    ChromeTracer::Instance().RecordEnd();
}