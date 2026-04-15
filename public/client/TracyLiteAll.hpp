#ifndef TRACYLITE_ALL_HPP
#define TRACYLITE_ALL_HPP
#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wpadded"
#endif

#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>
#include <functional>
#include <chrono>
#include "../common/TracyForceInline.hpp"

#ifdef _MSVC_LANG
#  define TRACY_CPP_VERSION _MSVC_LANG
#elif defined( __cplusplus )
#  define TRACY_CPP_VERSION __cplusplus
#else
#  define TRACY_CPP_VERSION 199711L
#endif

namespace tracy_internal
{
#if TRACY_CPP_VERSION >= 201402L
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
constexpr const char* str_end( const char* str ) { return *str ? str_end( str + 1 ) : str; }
constexpr bool str_slant( const char* str ) { return *str ? ( *str == '/' ? true : str_slant( str + 1 ) ) : false; }
constexpr const char* r_slant( const char* str ) { return *str == '/' ? ( str + 1 ) : r_slant( str - 1 ); }
constexpr const char* tracy_filename( const char* path ) { return str_slant( path ) ? r_slant( str_end( path ) ) : path; }
#endif
}

#if TRACY_CPP_VERSION >= 202002L
#  include <source_location>
#  define TraceFile std::source_location::current().file_name()
#else
#  define TraceFile ::tracy_internal::tracy_filename( __FILE__ )
#endif
#define TracyFunction __FUNCTION__
#include "TracyProfiler.hpp"
#include "../common/TracySystem.hpp"
namespace tracylite {
struct SourceLocationDataLite
{
    const char* name_;
    const char* function_;
    const char* file_;
    uint32_t line_;
    uint32_t color_;
};

enum class QueueTypeLite : uint8_t {
    kZoneBegin = 0,
    kZoneEnd = 1,
    kInstant = 2,
    kCounter = 3,
    kCounterDouble = 4,
    kMemAlloc = 5,
    kMemFree = 6
};

struct StringRef { uint32_t idx_; };

struct QueueHeaderLite
{
    uint64_t timestamp_;
    uint32_t threadId_;
    uint8_t type_;
    uint8_t reserved_;
    uint16_t size_;
};

struct QueueZoneBeginLite { QueueHeaderLite hdr_; const SourceLocationDataLite* srcloc_; };
struct QueueZoneEndLite { QueueHeaderLite hdr_; };
struct QueueInstantLite { QueueHeaderLite hdr_; StringRef nameRef_; StringRef scopeRef_; };
struct QueueCounterLite { QueueHeaderLite hdr_; StringRef nameRef_; int64_t value_; };

struct QueuePacketLite
{
    struct ZoneBeginPayload { const SourceLocationDataLite* srcloc_; };
    struct ZoneEndPayload { };
    struct InstantPayload { StringRef nameRef_; StringRef scopeRef_; };
    struct CounterPayload { StringRef nameRef_; int64_t value_; };
    struct CounterDoublePayload { StringRef nameRef_; double value_; };
    struct MemAllocPayload { uint64_t ptr_; uint64_t size_; };
    struct MemFreePayload { uint64_t ptr_; };

    uint64_t timestamp_ = 0;
    uint32_t threadId_ = 0;
    QueueTypeLite tag_ = QueueTypeLite::kZoneBegin;
    union Payload
    {
        ZoneBeginPayload zoneBegin_;
        ZoneEndPayload zoneEnd_;
        InstantPayload instant_;
        CounterPayload counter_;
        CounterDoublePayload counterDouble_;
        MemAllocPayload memAlloc_;
        MemFreePayload memFree_;

        Payload() {}
    } payload_;
};

class RingBuffer {
public:
    RingBuffer();
    explicit RingBuffer(size_t size);
    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    RingBuffer(RingBuffer&& other) noexcept;
    RingBuffer& operator=(RingBuffer&& other) noexcept;

    tracy_force_inline void Enqueue( const QueuePacketLite& packet )
    {
        auto* cur = mCur_;
        if( cur >= mEnd_ )
        {
            ++mDropped_;
            return;
        }
        *cur = packet;
        mCur_ = cur + 1;
    }

    tracy_force_inline QueuePacketLite* TryAllocSlot() noexcept
    {
        auto* cur = mCur_;
        if( cur >= mEnd_ )
        {
            ++mDropped_;
            return nullptr;
        }
        mCur_ = cur + 1;
        return cur;
    }

    bool TryDequeue( QueuePacketLite& packet );
    size_t GetSize() const;
    size_t GetDroppedCount() const { return mDropped_; }

private:
    QueuePacketLite* mBuffer_ = nullptr;
    QueuePacketLite* mCur_ = nullptr;
    QueuePacketLite* mEnd_ = nullptr;
    QueuePacketLite* mDeqCur_ = nullptr;
    size_t mDropped_ = 0;
};

class StringTable {
public:
    StringTable();
    StringRef Intern(const char* str);
    StringRef Intern(const std::string& str);
    const char* Get(uint32_t idx) const;
    const std::vector<std::string>& GetStrings() const { return mStrings_; }

private:
    std::vector<std::string> mStrings_;
    std::unordered_map<std::string, uint32_t> mStringMap_;
    std::unordered_map<const char*, uint32_t> mPtrMap_;
    std::unordered_multimap<uint64_t, uint32_t> mHashMap_;
    const char* mLastPtr_ = nullptr;
    uint32_t mLastPtrIdx_ = 0;
};

class Collector {
public:
    Collector( const Collector& ) = delete;
    Collector( Collector&& ) = delete;
    Collector& operator=( const Collector& ) = delete;
    Collector& operator=( Collector&& ) = delete;

    struct DrainPacketView
    {
        QueuePacketLite packet_;
        const StringTable* stringTable_;
    };

    static Collector& Instance();
    static void Initialize(size_t bufferSize = static_cast<size_t>( 8 ) * 1024 * 1024);

    static tracy_force_inline void ZoneBegin( const SourceLocationDataLite* srcloc ) noexcept
    {
        auto* slot = GetThreadState().buffer_.TryAllocSlot();
        if( !slot ) return;
        slot->tag_ = QueueTypeLite::kZoneBegin;
        slot->timestamp_ = tracy::Profiler::GetTime();
        slot->payload_.zoneBegin_.srcloc_ = srcloc;
    }

    static tracy_force_inline void ZoneBeginIf( const SourceLocationDataLite* srcloc, const bool active ) noexcept
    {
        if( active ) ZoneBegin( srcloc );
    }

    static tracy_force_inline void ZoneEnd() noexcept
    {
        auto* slot = GetThreadState().buffer_.TryAllocSlot();
        if( !slot ) return;
        slot->tag_ = QueueTypeLite::kZoneEnd;
        slot->timestamp_ = tracy::Profiler::GetTime();
    }

    static void Instant(const char* name, const char* scope = "thread");
    static void Counter(const char* name, int64_t value);
    static void Counter(const char* name, double value);
    static void MemAlloc(const void* ptr, size_t size) noexcept;
    static void MemFree(const void* ptr) noexcept;

    static void DrainAllPackets( std::vector<DrainPacketView>& out );

    static const uint8_t* GetBuffer();
    static size_t GetBufferSize();
    static size_t GetDroppedCount();


private:
    Collector();
    ~Collector();

    struct ThreadState
    {
        explicit ThreadState( size_t bufferSize )
            : buffer_( bufferSize )
            , threadId_( tracy::GetThreadHandle() )
        {
        }

        RingBuffer buffer_;
        StringTable stringTable_;
        uint32_t threadId_;
        ThreadState* next_ = nullptr;
    };

    static uint32_t GetThreadId();

    static tracy_force_inline ThreadState& GetThreadState() noexcept
    {
        // ReSharper disable once CppTooWideScope
        auto* s = sTlsState_;
        if( s ) return *s;
        return InitThreadStateCold();
    }

#if defined(_MSC_VER)
    __declspec(noinline) static ThreadState& InitThreadStateCold();
#else
    __attribute__((noinline, cold)) static ThreadState& InitThreadStateCold();
#endif

    static ThreadState& InitThreadState();

    static std::atomic<ThreadState*> sThreadStateHead_;
    static size_t sDefaultBufferSize_;
    static thread_local ThreadState* sTlsState_;
};

class ScopedZoneLite {
public:
    tracy_force_inline explicit ScopedZoneLite( const SourceLocationDataLite* srcloc ) noexcept
        : mActive_( true )
    {
        Collector::ZoneBegin( srcloc );
    }

    tracy_force_inline ScopedZoneLite( const SourceLocationDataLite* srcloc, const bool active ) noexcept
        : mActive_( active )
    {
        Collector::ZoneBeginIf( srcloc, active );
    }

    tracy_force_inline ~ScopedZoneLite() noexcept
    {
        if( mActive_ ) Collector::ZoneEnd();
    }

    ScopedZoneLite( const ScopedZoneLite& ) = delete;
    ScopedZoneLite( ScopedZoneLite&& ) = delete;
    ScopedZoneLite& operator=( const ScopedZoneLite& ) = delete;
    ScopedZoneLite& operator=( ScopedZoneLite&& ) = delete;

private:
    bool mActive_;
};

class ScopedZoneLiteIf {
public:
    tracy_force_inline ScopedZoneLiteIf( const SourceLocationDataLite* srcloc, const bool active ) noexcept
        : mActive_( active )
    {
        Collector::ZoneBeginIf( srcloc, active );
    }

    tracy_force_inline ~ScopedZoneLiteIf() noexcept
    {
        if( mActive_ ) Collector::ZoneEnd();
    }
    ScopedZoneLiteIf( const ScopedZoneLiteIf& ) = delete;
    ScopedZoneLiteIf( ScopedZoneLiteIf&& ) = delete;
    ScopedZoneLiteIf& operator=( const ScopedZoneLiteIf& ) = delete;
    ScopedZoneLiteIf& operator=( ScopedZoneLiteIf&& ) = delete;

private:
    bool mActive_;
};

using ScopedZone = ScopedZoneLite;
using ScopedZoneIf = ScopedZoneLiteIf;

struct DumpConfig {
    size_t maxBufferMb_ = 64;
    bool exportOnDestroy_ = true;
};

class DumpManager {
public:
    static DumpManager& Instance();
    void Initialize(const DumpConfig& config = DumpConfig());
    bool ExportNow();
    bool ExportAsPerfetto(const char* filepath = nullptr);

    using PreExportCallback = std::function<void()>;
    void SetPreExportCallback( const PreExportCallback& cb);

    using PostExportCallback = std::function<void(const std::string& path)>;
    void SetPostExportCallback( const PostExportCallback& cb);

    const DumpConfig& GetConfig() const { return mConfig_; }
    void UpdateConfig(const DumpConfig& config);
    void Shutdown();

    struct Stats {
        size_t eventCount_ = 0;
        size_t bufferSize_ = 0;
        size_t exportCount_ = 0;
        std::chrono::system_clock::time_point lastExportTime_;
    };
    Stats GetStats() const;
    DumpManager( const DumpManager& ) = delete;
    DumpManager( DumpManager&& ) = delete;
    DumpManager& operator=( const DumpManager& ) = delete;
    DumpManager& operator=( DumpManager&& ) = delete;

private:
    DumpManager() = default;
    ~DumpManager() noexcept;

    void ExportNowOnce();

    DumpConfig mConfig_;
    bool mInitialized_ = false;

    PreExportCallback mPreExportCb_;
    PostExportCallback mPostExportCb_;

    size_t mExportCount_ = 0;
    std::chrono::system_clock::time_point mLastExportTime_;
    std::once_flag mExportOnce_;
};

class AutoDumpInit {
public:
    static AutoDumpInit& Instance(const DumpConfig& config = DumpConfig());
    static bool Export(const std::string& filepath);
    AutoDumpInit( const AutoDumpInit& ) = delete;
    AutoDumpInit( AutoDumpInit&& ) = delete;
    AutoDumpInit& operator=( const AutoDumpInit& ) = delete;
    AutoDumpInit& operator=( AutoDumpInit&& ) = delete;

private:
    explicit AutoDumpInit(const DumpConfig& config);
    ~AutoDumpInit() noexcept;
};

#define TRACYLITE_AUTO_DUMP(...) \
    static auto& __tracylite_auto_dump = ::tracylite::AutoDumpInit::Instance(__VA_ARGS__)

#define TRACYLITE_EXPORT() ::tracylite::DumpManager::Instance().ExportNow()
#define TRACYLITE_EXPORT_PERFETTO(filepath) ::tracylite::DumpManager::Instance().ExportAsPerfetto(filepath)
#define TRACYLITE_SET_DUMP_CONFIG(config) ::tracylite::DumpManager::Instance().UpdateConfig(config)
#define TRACYLITE_GET_DUMP_STATS() ::tracylite::DumpManager::Instance().GetStats()
#define TRACYLITE_SET_PRE_EXPORT(callback) ::tracylite::DumpManager::Instance().SetPreExportCallback(callback)
#define TRACYLITE_SET_POST_EXPORT(callback) ::tracylite::DumpManager::Instance().SetPostExportCallback(callback)

}  // namespace tracylite

#ifndef TracyLiteConcat
#  define TracyLiteConcat(x,y) TracyLiteConcatIndirect(x,y)
#  define TracyLiteConcatIndirect(x,y) x##y
#endif

#define TRACYLITE_ZONE(name) \
    static const ::tracylite::SourceLocationDataLite TracyLiteConcat(__tracylite_srcloc_, __LINE__) = { name, TracyFunction, TraceFile, __LINE__, 0 }; \
    ::tracylite::ScopedZoneLite TracyLiteConcat(__tracylite_zone_, __LINE__)( &TracyLiteConcat(__tracylite_srcloc_, __LINE__) )

#define TRACYLITE_ZONE_IF(name, active) \
    static const ::tracylite::SourceLocationDataLite TracyLiteConcat(__tracylite_srcloc_, __LINE__) = { name, TracyFunction, TraceFile, __LINE__, 0 }; \
    ::tracylite::ScopedZoneLiteIf TracyLiteConcat(__tracylite_zone_if_, __LINE__)( &TracyLiteConcat(__tracylite_srcloc_, __LINE__), (active) )

#define TRACYLITE_ZONE_NAMED(name, color) \
    static const ::tracylite::SourceLocationDataLite TracyLiteConcat(__tracylite_srcloc_, __LINE__) = { name, TracyFunction, TraceFile, __LINE__, color }; \
    ::tracylite::ScopedZoneLite TracyLiteConcat(__tracylite_zone_, __LINE__)( &TracyLiteConcat(__tracylite_srcloc_, __LINE__) )

#define TRACYLITE_ZONE_NAMED_IF(name, color, active) \
    static const ::tracylite::SourceLocationDataLite TracyLiteConcat(__tracylite_srcloc_, __LINE__) = { name, TracyFunction, TraceFile, __LINE__, color }; \
    ::tracylite::ScopedZoneLiteIf TracyLiteConcat(__tracylite_zone_if_, __LINE__)( &TracyLiteConcat(__tracylite_srcloc_, __LINE__), (active) )

#define TRACYLITE_INSTANT(name) tracylite::Collector::Instance().Instant(name)
#define TRACYLITE_COUNTER(name, value) tracylite::Collector::Instance().Counter(name, value)
#define TRACYLITE_MALLOC(ptr, size) tracylite::Collector::Instance().MemAlloc(ptr, size)
#define TRACYLITE_FREE(ptr) tracylite::Collector::Instance().MemFree(ptr)

#ifdef __clang__
#  pragma clang diagnostic pop
#endif

#endif
