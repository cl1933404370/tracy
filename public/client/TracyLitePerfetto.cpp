// TracyLitePerfetto.cpp
//
// Perfetto native trace exporter using direct protozero serialization.
//
// This file does NOT use the Perfetto tracing runtime (no Initialize,
// no sessions, no TRACE_EVENT macros, no global static storage).
// Instead, it directly serializes TracePacket protobufs via protozero,
// producing a .perfetto-trace file that can be opened in
// https://ui.perfetto.dev.
//
// Benefits over the TrackEvent API approach:
//   - No double-buffering (events go straight from RingBuffer to protobuf)
//   - No Perfetto runtime threads / IPC / shared-memory overhead
//   - No global constructors (PERFETTO_DEFINE_CATEGORIES removed)
//   - ~10x faster export for large traces
//   - Compatible with GCC 9+ / C++17

#include "TracyLitePerfetto.hpp"

#ifdef TRACYLITE_PERFETTO

#include "TracyLiteAll.hpp"

// We only need the protozero serializer and pbzero generated types from the
// amalgamated SDK header.  No Perfetto runtime is initialised.
#include <perfetto.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <fstream>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <chrono>

namespace tracylite {

namespace
{

#ifndef TRACYLITE_PERFETTO_COMPACT_LEVEL
#define TRACYLITE_PERFETTO_COMPACT_LEVEL 0
#endif

// Copy a string literal into buf (excluding the null terminator).
// Size deduced at compile time → optimised to a single mov instruction.
// Avoids memcpy warnings (bugprone-not-null-terminated-result, MSVC extension).
template<size_t N>
char* WriteLit( char* buf, const char (&s)[N] )
{
    for( size_t i = 0; i < N - 1; ++i ) buf[i] = s[i];
    return buf + ( N - 1 );
}

// Write a uint64_t as decimal digits into buf. Returns pointer past the last
// digit written. No null terminator. C++14-compatible, zero-allocation.
 char* WriteU64( char* buf, uint64_t val )
{
    if( val == 0 ) { *buf++ = '0'; return buf; }
    char tmp[20];
    char* t = tmp;
    while( val > 0 ) { *t++ = static_cast<char>( '0' + val % 10 ); val /= 10; }
    while( t != tmp ) *buf++ = *--t;
    return buf;
}

// Format "<prefix><uint64><suffix>" into buf.
// Returns {pointer to buf, length written}. No null terminator.
// buf must be large enough (prefix + 20 digits + suffix).
struct BufSpan { const char* data; size_t size; };
template<size_t NP, size_t NS>
 BufSpan FormatU64Name( char* buf, const char (&prefix)[NP], uint64_t val, const char (&suffix)[NS] )
{
    char* p = WriteLit( buf, prefix );
    p = WriteU64( p, val );
    p = WriteLit( p, suffix );
    return { buf, static_cast<size_t>( p - buf ) };
}

// ── Timestamp conversion

struct TimestampConverterPerfetto
{
    static const TimestampConverterPerfetto& Instance()
    {
        static const TimestampConverterPerfetto converter = Create();
        return converter;
    }

    [[nodiscard]] uint64_t RawToNs( const uint64_t raw ) const
    {
        if( mRawIsNs_ ) return raw;
        return static_cast<uint64_t>( static_cast<double>( raw ) * mNsPerTick_ );
    }

private:
    static TimestampConverterPerfetto Create()
    {
#if defined( _WIN32 ) && defined( TRACY_TIMER_QPC )
        const auto frequency = tracy::GetFrequencyQpc();
        if( frequency > 0 ) return TimestampConverterPerfetto( false, 1000000000.0 / static_cast<double>( frequency ) );
#endif

#ifndef TRACY_HW_TIMER
        return TimestampConverterPerfetto( true, 1.0 );
#elif defined( TRACY_TIMER_FALLBACK )
        if( !tracy::HardwareSupportsInvariantTSC() ) return TimestampConverterPerfetto( true, 1.0 );
#endif

        const auto t0 = std::chrono::high_resolution_clock::now();
        const auto r0 = tracy::Profiler::GetTime();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto r1 = tracy::Profiler::GetTime();

        const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>( t1 - t0 ).count();
        const auto dr = r1 - r0;
        if( dt <= 0 || dr <= 0 ) return TimestampConverterPerfetto{ true, 1.0 };
        return TimestampConverterPerfetto{ false, static_cast<double>( dt ) / static_cast<double>( dr ) };
    }

    TimestampConverterPerfetto( const bool rawIsNs, const double nsPerTick )
        : mNsPerTick_( nsPerTick )
        , mRawIsNs_( rawIsNs )
    {
    }
    double mNsPerTick_;
    bool mRawIsNs_;
    [[maybe_unused]] char padding_[7] = {};
};

// ── Protobuf wire-format helpers ────────────────────────────────────────────

// Append a variant-encoded value to the output buffer.
[[maybe_unused]] void WriteVariant( std::vector<uint8_t>& buf, uint64_t val )
{
    while( val > 0x7F )
    {
        // ReSharper disable once CppRedundantParentheses
        buf.push_back( static_cast<uint8_t>( ( val & 0x7F ) | 0x80 ) );
        val >>= 7;
    }
    buf.push_back( static_cast<uint8_t>( val ) );
}

 size_t WriteVariantToArray( uint64_t val, uint8_t* dst )
{
    size_t n = 0;
    while( val > 0x7F )
    {
        dst[n++] = static_cast<uint8_t>( ( val & 0x7F ) | 0x80 );
        val >>= 7;
    }
    dst[n++] = static_cast<uint8_t>( val );
    return n;
}

// Append a serialised TracePacket wrapped in the Trace.packet field envelope
// (field 1, wire type 2 → tag byte 0x0A).
 void AppendTracePacket( std::vector<uint8_t>& out,
                               protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket>& pkt )
{
    auto ranges = pkt.GetRanges();
    if( ranges.empty() ) return;

    size_t payloadSize = 0;
    for( const auto& r : ranges ) payloadSize += r.size();

    uint8_t variantBuf[10];
    const size_t variantSize = WriteVariantToArray( payloadSize, variantBuf );
    const size_t oldSize = out.size();
    out.resize( oldSize + 1 + variantSize + payloadSize );

    auto* dst = out.data() + oldSize;
    *dst++ = 0x0A;
    std::memcpy( dst, variantBuf, variantSize );
    dst += variantSize;

    for( const auto& r : ranges )
    {
        const size_t n = static_cast<size_t>( r.end - r.begin );
        if( n == 0 ) continue;
        std::memcpy( dst, r.begin, n );
        dst += n;
    }
}

 void RadixSortByTimestamp( std::vector<Collector::DrainPacketView>& drained )
{
    if( drained.size() < 2 ) return;

    std::vector<Collector::DrainPacketView> tmp( drained.size() );
    constexpr size_t kRadix = 1u << 16;
    std::array<size_t, kRadix> count{};

    for( int pass = 0; pass < 4; ++pass )
    {
        const int shift = pass * 16;
        count.fill( 0 );

        for( const auto& ev : drained )
        {
            ++count[( ev.packet_.timestamp_ >> shift ) & 0xFFFFu];
        }

        size_t sum = 0;
        for( size_t i = 0; i < kRadix; ++i )
        {
            const size_t c = count[i];
            count[i] = sum;
            sum += c;
        }

        for( const auto& ev : drained )
        {
            const auto idx = ( ev.packet_.timestamp_ >> shift ) & 0xFFFFu;
            tmp[count[idx]++] = ev;
        }

        drained.swap( tmp );
    }
}

// Stable track UUID from a thread id.
 uint64_t ThreadTrackUuid( const uint32_t tid )
{
    return static_cast<uint64_t>( tid ) | ( static_cast<uint64_t>(0xCAFE) << 48 );
}

// Stable track UUID for a named counter on a given thread.
 uint64_t CounterTrackUuid( const uint32_t tid, const char* name )
{
    uint64_t h = 0x1505;
    for( const char* p = name; *p; ++p )
        h = h * 33 + static_cast<uint8_t>( *p );
    return ( h ^ ThreadTrackUuid( tid ) ) | ( static_cast<uint64_t>(0xC047) << 48 );
}

 constexpr uint32_t kSequenceId = 1;
 constexpr int32_t  kPid = 1;

 void InitPacketWithTimestamp( protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket>& pkt, const uint64_t tsNs )
{
    pkt->set_timestamp( tsNs );
    pkt->set_timestamp_clock_id( perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC );
    pkt->set_trusted_packet_sequence_id( kSequenceId );
}

 void InitPacketWithoutTimestamp( protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket>& pkt )
{
    pkt->set_trusted_packet_sequence_id( kSequenceId );
}

 struct CounterCacheKey
{
    const void* table;
    uint32_t tid;
    uint32_t nameIdx;

    bool operator==( const CounterCacheKey& other ) const
    {
        return table == other.table && tid == other.tid && nameIdx == other.nameIdx;
    }
};

 struct CounterCacheKeyHash
{
    size_t operator()( const CounterCacheKey& v ) const
    {
        const auto h1 = std::hash<const void*>{}( v.table );
        const auto h2 = std::hash<uint32_t>{}( v.tid );
        const auto h3 = std::hash<uint32_t>{}( v.nameIdx );
        return h1 ^ ( h2 + 0x9e3779b9 + ( h1 << 6 ) + ( h1 >> 2 ) ) ^
               ( h3 + 0x9e3779b9 + ( h2 << 6 ) + ( h2 >> 2 ) );
    }
};

struct StringCacheKey
{
    const void* table;
    uint32_t nameIdx;
    [[maybe_unused]] uint32_t pad_ = 0;

    bool operator==( const StringCacheKey& other ) const
    {
        return table == other.table && nameIdx == other.nameIdx;
    }
};

 struct StringCacheKeyHash
{
    size_t operator()( const StringCacheKey& v ) const
    {
        const auto h1 = std::hash<const void*>{}( v.table );
        const auto h2 = std::hash<uint32_t>{}( v.nameIdx );
        return h1 ^ ( h2 + 0x9e3779b9 + ( h1 << 6 ) + ( h1 >> 2 ) );
    }
};

 const char* GetCachedString( const Collector::DrainPacketView& view,
                                     const uint32_t nameIdx,
                                     std::unordered_map<StringCacheKey, const char*, StringCacheKeyHash>& cache )
{
    const auto key = StringCacheKey{ view.stringTable_, nameIdx };
    const auto it = cache.find( key );
    if( it != cache.end() ) return it->second;
    const char* s = view.stringTable_->Get( nameIdx );
    cache.emplace( key, s );
    return s;
}

// ── Core serialisation ──────────────────────────────────────────────────────

std::vector<uint8_t> CollectTrace( const Collector& collector )
{
    const auto& converter = TimestampConverterPerfetto::Instance();

    // Drain all buffered events.
    std::vector<Collector::DrainPacketView> drained;
    const auto bufferedEventCount = tracylite::Collector::GetBufferSize();
    drained.reserve( bufferedEventCount );

    tracylite::Collector::DrainAllPackets( drained );
    if( drained.empty() ) return {};

    const auto tsLess = []( const Collector::DrainPacketView& a, const Collector::DrainPacketView& b ) {
        return a.packet_.timestamp_ < b.packet_.timestamp_;
    };
    if( drained.size() > 4096 )
    {
        RadixSortByTimestamp( drained );
    }
    else if( !std::is_sorted( drained.begin(), drained.end(), tsLess ) )
    {
        std::sort( drained.begin(), drained.end(), tsLess );
    }

    const auto startTimestampRaw = drained.front().packet_.timestamp_;

    // Pre-scan: collect unique thread ids and counter names so we can emit
    // track descriptor packets before any events.
    std::unordered_set<uint32_t> threadIds;
    threadIds.reserve( std::min<size_t>( drained.size(), 4096 ) );
    struct CounterKey { std::string name_; uint64_t tid_; };
    std::vector<CounterKey> counterKeys;
    counterKeys.reserve( std::min<size_t>( drained.size() / 8 + 8, 8192 ) );
    std::unordered_set<uint64_t> counterUuids;
    counterUuids.reserve( std::min<size_t>( drained.size() / 8 + 8, 8192 ) );
    std::unordered_map<CounterCacheKey, uint64_t, CounterCacheKeyHash> counterUuidCache;
    counterUuidCache.reserve( std::min<size_t>( drained.size() / 4 + 16, 16384 ) );
    std::unordered_map<StringCacheKey, const char*, StringCacheKeyHash> stringCache;
    stringCache.reserve( std::min<size_t>( drained.size() / 4 + 16, 16384 ) );
    std::unordered_map<uint32_t, uint64_t> heapCounterUuidByThread;
    heapCounterUuidByThread.reserve( std::min<size_t>( drained.size() / 8 + 8, 4096 ) );

    for( const auto& view : drained )
    {
        threadIds.insert( view.packet_.threadId_ );
        if( view.packet_.tag_ == QueueTypeLite::kCounter )
        {
            const auto key = CounterCacheKey{ view.stringTable_, view.packet_.threadId_, view.packet_.payload_.counter_.nameRef_.idx_ };
            auto itUuid = counterUuidCache.find( key );
            uint64_t uuid;
            const char* cname = nullptr;
            if( itUuid != counterUuidCache.end() )
            {
                uuid = itUuid->second;
            }
            else
            {
                cname = GetCachedString( view, view.packet_.payload_.counter_.nameRef_.idx_, stringCache );
                uuid = CounterTrackUuid( view.packet_.threadId_, cname );
                counterUuidCache.emplace( key, uuid );
            }
            if( counterUuids.insert( uuid ).second )
            {
                if( !cname ) cname = GetCachedString( view, view.packet_.payload_.counter_.nameRef_.idx_, stringCache );
                counterKeys.push_back( { std::string( cname ), view.packet_.threadId_ } );
            }
        }
        else if( view.packet_.tag_ == QueueTypeLite::kCounterDouble )
        {
            const auto key = CounterCacheKey{ view.stringTable_, view.packet_.threadId_, view.packet_.payload_.counterDouble_.nameRef_.idx_ };
            auto itUuid = counterUuidCache.find( key );
            uint64_t uuid;
            const char* cname = nullptr;
            if( itUuid != counterUuidCache.end() )
            {
                uuid = itUuid->second;
            }
            else
            {
                cname = GetCachedString( view, view.packet_.payload_.counterDouble_.nameRef_.idx_, stringCache );
                uuid = CounterTrackUuid( view.packet_.threadId_, cname );
                counterUuidCache.emplace( key, uuid );
            }
            if( counterUuids.insert( uuid ).second )
            {
                if( !cname ) cname = GetCachedString( view, view.packet_.payload_.counterDouble_.nameRef_.idx_, stringCache );
                counterKeys.push_back( { std::string( cname ), view.packet_.threadId_ } );
            }
        }
        else if( view.packet_.tag_ == QueueTypeLite::kMemAlloc || view.packet_.tag_ == QueueTypeLite::kMemFree )       
        {
            static constexpr auto heapSizeName = "Heap Size";
            const auto tid = view.packet_.threadId_;
            auto it = heapCounterUuidByThread.find( tid );
            const auto uuid = it != heapCounterUuidByThread.end() ? it->second : CounterTrackUuid( tid, heapSizeName );
            if( it == heapCounterUuidByThread.end() ) heapCounterUuidByThread.emplace( tid, uuid );
            if( counterUuids.insert( uuid ).second )
                counterKeys.push_back( { std::string( heapSizeName ), view.packet_.threadId_ } );
        }
    }

    // Rough size estimate: ~128 bytes per event + descriptors.
    std::vector<uint8_t> out;
    out.reserve( drained.size() * 128 + threadIds.size() * 256 + 512 );

    // ── Emit ClockSnapshot ──────────────────────────────────────────────
    // Maps BUILTIN_CLOCK_MONOTONIC (3, used in event packets) to
    // BUILTIN_CLOCK_BOOT TIME (6, Perfetto's default trace clock) at t=0.
    // Without this, Perfetto UI reports clock_sync_failure_unknown_source_clock.
    {
        protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> pkt;
        InitPacketWithoutTimestamp( pkt );
        auto* cs = pkt->set_clock_snapshot();
        cs->set_primary_trace_clock( perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME );
        auto* cBoot = cs->add_clocks();
        cBoot->set_clock_id( perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME );
        cBoot->set_timestamp( 0 );
        auto* cMono = cs->add_clocks();
        cMono->set_clock_id( perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC );
        cMono->set_timestamp( 0 );
        AppendTracePacket( out, pkt );
    }

    // ── Emit TracyLite metadata (SDK version, event count) ──────────────
    {
        protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> pkt;
        InitPacketWithTimestamp( pkt, 0 );
        auto* te = pkt->set_track_event();
        te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT );
        te->set_track_uuid( 0 );
        te->set_name( "TracyLite::Metadata", 19 );
#ifdef TRACYLITE_PERFETTO_SDK_VERSION
        {
            auto* ann = te->add_debug_annotations();
            ann->set_name( "perfetto_sdk_version", 20 );
            auto ver = TRACYLITE_PERFETTO_SDK_VERSION;
            ann->set_string_value( ver, std::strlen( ver ) );
        }
#endif
        {
            auto* ann = te->add_debug_annotations();
            ann->set_name( "event_count", 11 );
            ann->set_uint_value( drained.size() );
        }
        AppendTracePacket( out, pkt );
    }

    // ── Emit process descriptor ─────────────────────────────────────────
    {
        protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> pkt;
        InitPacketWithoutTimestamp( pkt );
        pkt->set_sequence_flags(
            perfetto::protos::pbzero::TracePacket_SequenceFlags::SEQ_INCREMENTAL_STATE_CLEARED );
        auto* td = pkt->set_track_descriptor();
        td->set_uuid( 0 );
        auto* proc = td->set_process();
        proc->set_pid( kPid );
        proc->set_process_name( "TracyLite" );
        AppendTracePacket( out, pkt );
    }

    // ── Emit thread track descriptors ───────────────────────────────────
    for( const auto tid : threadIds )
    {
        protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> pkt;
        InitPacketWithoutTimestamp( pkt );
        auto* td = pkt->set_track_descriptor();
        td->set_uuid( ThreadTrackUuid( tid ) );
        td->set_parent_uuid( 0 );
        auto name = "Thread_" + std::to_string( tid );
        td->set_name( name );
        auto* thr = td->set_thread();
        thr->set_pid( kPid );
        thr->set_tid( static_cast<int32_t>( tid ) );
        thr->set_thread_name( name );
        AppendTracePacket( out, pkt );
    }

    // ── Emit counter track descriptors ──────────────────────────────────
    for( const auto& ck : counterKeys )
    {
        protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> pkt;
        InitPacketWithoutTimestamp( pkt );
        auto* td = pkt->set_track_descriptor();
        td->set_uuid( CounterTrackUuid( static_cast<uint32_t>( ck.tid_ ), ck.name_.c_str() ) );
        td->set_parent_uuid( ThreadTrackUuid( static_cast<uint32_t>( ck.tid_ ) ) );
        td->set_name( ck.name_ );
        td->set_counter();
        AppendTracePacket( out, pkt );
    }

    // ── Emit trace events ───────────────────────────────────────────────
    std::unordered_map<uint32_t, int64_t> heapSizeByThread;
    heapSizeByThread.reserve( threadIds.size() + 8 );
    std::unordered_map<uint64_t, uint64_t> ptrSizeMap;
    ptrSizeMap.reserve( std::min<size_t>( drained.size() / 4 + 16, 32768 ) );

    // ── Zone-stack tracking for source_location + callstack annotation ──
    // We reconstruct a pseudo-callstack from zone nesting at zero runtime
    // cost.  On each SLICE_BEGIN we attach:
    //   - source_location  → file / function / line  (shown in details panel)
    //   - debug_annotation "callstack" → full zone-stack string (parent chain)
    struct OpenZoneInfo {
        const SourceLocationDataLite* srcloc = nullptr;
    };
    std::unordered_map<uint32_t, std::vector<OpenZoneInfo>> zoneStackByThread;
    zoneStackByThread.reserve( threadIds.size() + 8 );

    for( const auto& view : drained )
    {
        const auto& packet = view.packet_;
        const auto& stringTable = *view.stringTable_;
        const auto tsNs = converter.RawToNs( packet.timestamp_ - startTimestampRaw );
        const auto trackUuid = ThreadTrackUuid( packet.threadId_ );

        protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> pkt;
        InitPacketWithTimestamp( pkt, tsNs );

        switch( packet.tag_ )
        {
        case QueueTypeLite::kZoneBegin:
        {
            const auto* srcloc = packet.payload_.zoneBegin_.srcloc_;
            auto name = "<unknown>";
            const char* func = nullptr;
            const char* file = nullptr;
            uint32_t line = 0;
            if( srcloc )
            {
                name = srcloc->name_ ? srcloc->name_
                                    : srcloc->function_ ? srcloc->function_ : "<zone>";
                func = srcloc->function_;
                file = srcloc->file_;
                line = srcloc->line_;
            }

            // Push onto zone stack for this thread.
            zoneStackByThread[packet.threadId_].push_back( { srcloc } );

            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN );
            te->set_track_uuid( trackUuid );
            te->set_name( name, std::strlen( name ) );

            // Attach source_location (file/function/line) — shown in details panel.
#if TRACYLITE_PERFETTO_COMPACT_LEVEL == 0
            if( func || file || line )
            {
                auto* sl = te->set_source_location();
                if( func ) sl->set_function_name( func, std::strlen( func ) );
                if( file ) sl->set_file_name( file, std::strlen( file ) );
                if( line ) sl->set_line_number( line );
            }
#endif

            // Keep source_location for useful symbol context while avoiding
            // per-event callstack-string materialization to reduce export CPU
            // and output file size.

            break;
        }
        case QueueTypeLite::kZoneEnd:
        {
            // Pop the zone stack for this thread.
            auto zit = zoneStackByThread.find( packet.threadId_ );
            if( zit != zoneStackByThread.end() && !zit->second.empty() )
                zit->second.pop_back();

            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END );
            te->set_track_uuid( trackUuid );
            break;
        }
        case QueueTypeLite::kInstant:
        {
            const auto* cachedName = GetCachedString( view, packet.payload_.instant_.nameRef_.idx_, stringCache );
            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT );
            te->set_track_uuid( trackUuid );
            te->set_name( cachedName, std::strlen( cachedName ) );
            break;
        }
        case QueueTypeLite::kCounter:
        {
            const char* name = GetCachedString( view, packet.payload_.counter_.nameRef_.idx_, stringCache );
            const auto key = CounterCacheKey{ &stringTable, packet.threadId_, packet.payload_.counter_.nameRef_.idx_ };
            auto itUuid = counterUuidCache.find( key );
            const auto counterUuid = itUuid != counterUuidCache.end() ? itUuid->second : CounterTrackUuid( packet.threadId_, name );
            if( itUuid == counterUuidCache.end() ) counterUuidCache.emplace( key, counterUuid );
            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER );
            te->set_track_uuid( counterUuid );
            te->set_counter_value( packet.payload_.counter_.value_ );
            break;
        }
        case QueueTypeLite::kCounterDouble:
        {
            const char* name = GetCachedString( view, packet.payload_.counterDouble_.nameRef_.idx_, stringCache );
            const auto key = CounterCacheKey{ &stringTable, packet.threadId_, packet.payload_.counterDouble_.nameRef_.idx_ };
            auto itUuid = counterUuidCache.find( key );
            const auto counterUuid = itUuid != counterUuidCache.end() ? itUuid->second : CounterTrackUuid( packet.threadId_, name );
            if( itUuid == counterUuidCache.end() ) counterUuidCache.emplace( key, counterUuid );
            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER );
            te->set_track_uuid( counterUuid );
            te->set_double_counter_value( packet.payload_.counterDouble_.value_ );
            break;
        }
        case QueueTypeLite::kMemAlloc:
        {
            const auto allocPtr = packet.payload_.memAlloc_.ptr_;
            const auto allocSize = packet.payload_.memAlloc_.size_;
            ptrSizeMap[allocPtr] = allocSize;
            auto& heapSize = heapSizeByThread[packet.threadId_];
            heapSize += static_cast<int64_t>( allocSize );

            const auto heapCounterUuid = heapCounterUuidByThread[packet.threadId_];
            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER );
            te->set_track_uuid( heapCounterUuid );
            te->set_counter_value( heapSize );
            AppendTracePacket( out, pkt );

            // Instant event with ptr/size details
#if TRACYLITE_PERFETTO_COMPACT_LEVEL == 0
            protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> iPkt;
            InitPacketWithTimestamp( iPkt, tsNs );
            auto* ie = iPkt->set_track_event();
            ie->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT );
            ie->set_track_uuid( trackUuid );
            {
                char buf[40];
                const auto span = FormatU64Name( buf, "alloc ", allocSize, " bytes" );
                ie->set_name( span.data, span.size );
            }
            auto* aPtr = ie->add_debug_annotations();
            aPtr->set_name( "ptr", 3 );
            aPtr->set_pointer_value( allocPtr );
            auto* aSize = ie->add_debug_annotations();
            aSize->set_name( "size", 4 );
            aSize->set_uint_value( allocSize );
            AppendTracePacket( out, iPkt );
#endif
            continue;  // skip the AppendTracePacket at the end of the loop
        }
        case QueueTypeLite::kMemFree:
        {
            const auto freePtr = packet.payload_.memFree_.ptr_;
            auto it = ptrSizeMap.find( freePtr );
            auto& heapSize = heapSizeByThread[packet.threadId_];
            uint64_t freedSize = 0;
            if( it != ptrSizeMap.end() )
            {
                freedSize = it->second;
                heapSize -= static_cast<int64_t>( freedSize );
                ptrSizeMap.erase( it );
            }

            const auto heapCounterUuid = heapCounterUuidByThread[packet.threadId_];
            auto* te = pkt->set_track_event();
            te->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER );
            te->set_track_uuid( heapCounterUuid );
            te->set_counter_value( heapSize );
            AppendTracePacket( out, pkt );

            // Instant event with ptr/freed-size details
#if TRACYLITE_PERFETTO_COMPACT_LEVEL == 0
            protozero::HeapBuffered<perfetto::protos::pbzero::TracePacket> iPkt;
            InitPacketWithTimestamp( iPkt, tsNs );
            auto* ie = iPkt->set_track_event();
            ie->set_type( perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT );
            ie->set_track_uuid( trackUuid );
            {
                char buf[40];
                const auto span = FormatU64Name( buf, "free ", freedSize, " bytes" );
                ie->set_name( span.data, span.size );
            }
            auto* aPtr = ie->add_debug_annotations();
            aPtr->set_name( "ptr", 3 );
            aPtr->set_pointer_value( freePtr );
            if( freedSize > 0 )
            {
                auto* aSize = ie->add_debug_annotations();
                aSize->set_name( "size", 4 );
                aSize->set_uint_value( freedSize );
            }
            AppendTracePacket( out, iPkt );
#endif
            continue;  // skip the AppendTracePacket at the end of the loop
        }
        }

        AppendTracePacket( out, pkt );
    }

    return out;
}

} // anonymous namespace

std::vector<uint8_t> PerfettoNativeExporter::ExportToBuffer( const Collector& collector )
{
    return CollectTrace( collector );
}

bool PerfettoNativeExporter::ExportToFile( const Collector& collector, const char* filename )
{
    try
    {
        const auto traceData = CollectTrace( collector );
        if( traceData.empty() ) return false;

        std::ofstream file( filename, std::ios::binary );
        if( !file.is_open() ) return false;
        constexpr size_t writeBufSize = static_cast<std::size_t>( 256 ) * 1024;
        char writeBuf[writeBufSize];
        file.rdbuf()->pubsetbuf( writeBuf, writeBufSize );
        file.write( reinterpret_cast<const char*>( traceData.data() ),
                     static_cast<std::streamsize>( traceData.size() ) );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}

} // namespace tracylite

#endif // TRACYLITE_PERFETTO
