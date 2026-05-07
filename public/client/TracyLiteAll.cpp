#include "TracyLiteAll.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <algorithm>
#include <cstdio>
#include <cstdlib>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <errno.h>
#include <thread>
#include <unordered_map>
#include <exception>
#include "TracyLitePerfetto.hpp"

#ifdef _WIN32
#  include <direct.h>
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace tracy
{
namespace detail
{
TRACY_API uint32_t GetThreadHandleImpl(); // NOLINT(readability-redundant-declaration)
}
}

namespace tracylite {
namespace
{
void WarmupTimestampConverter();

std::string GetAscendProcessLogPath()
{
    // Validate an ASCEND_PROCESS_LOG_PATH override before honouring it.
    // Reject anything that:
    //   - is not an absolute POSIX path (must start with '/')
    //   - contains a '..' segment (directory traversal)
    //   - contains NUL/CR/LF or any control character
    //   - is longer than a sane limit
    // On any failure we fall back to the built-in default. This avoids the
    // attack where setting ASCEND_PROCESS_LOG_PATH=/etc/cron.d/ would let an
    // attacker drop trace files into a sensitive system directory.
    auto IsLogPathSafe = []( const char* p ) -> bool {
        if( !p || p[0] != '/' ) return false;
        size_t len = 0;
        for( const char* q = p; *q; ++q, ++len )
        {
            const auto c = static_cast<unsigned char>( *q );
            if( c < 0x20 || c == 0x7F ) return false;
            if( len > 4096 ) return false;
        }
        // Reject "..", "/..", "../" or "/../" segments.
        size_t i = 0;
        while( i < len )
        {
            if( p[i] == '/' ) { ++i; continue; }
            const char* segStart = p + i;
            size_t segLen = 0;
            while( i < len && p[i] != '/' ) { ++i; ++segLen; }
            if( segLen == 2 && segStart[0] == '.' && segStart[1] == '.' )
                return false;
        }
        return true;
    };

    std::string path;
#ifdef __linux__
#  ifndef CCL_KERNEL_AICPU
    const char* env = std::getenv( "ASCEND_PROCESS_LOG_PATH" );
    if( env && env[0] != '\0' && IsLogPathSafe( env ) )
    {
        path = env;
    }
    else
    {
        path = "/root/ascend/log";
    }
#  else
    path = "/var/log/npu/slog";
#  endif
    if( !path.empty() && path.back() != '/' )
    {
        path.push_back( '/' );
    }
#else
    (void)IsLogPathSafe;
#endif

    auto CreateDirectories = []( const std::string& dir ) -> bool {
        if( dir.empty() ) return false;
        std::string p = dir;
        std::replace( p.begin(), p.end(), '\\', '/' );
        std::string cur;
        size_t i = 0;
        if( p.size() >= 2 && p[1] == ':' )
        {
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

            if( cur != "/" && ( cur.size() != 2 || cur[1] != ':' ) )
            {
#ifdef _WIN32
                const int r = _mkdir( cur.c_str() );
#else
                const int r = mkdir( cur.c_str(), 0755 );
#endif
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

void LogException( const char* where, const char* what = nullptr )
{
    if( what ) std::printf( "%s: %s\n", where, what );
    else std::printf( "%s\n", where );
}
}

RingBuffer::RingBuffer()
    : RingBuffer( 1024 )
{
}

RingBuffer::RingBuffer( const size_t size)
{
    const auto cap = size > 0 ? size : static_cast<size_t>(1024);
    mBuffer_ = static_cast<QueuePacketLite*>( tracy::tracy_malloc( cap * sizeof( QueuePacketLite ) ) );
    if( mBuffer_ )
    {
        mCur_ = mBuffer_;
        mEnd_ = mBuffer_ + cap;
        mDeqCur_ = mBuffer_;
        // Touch every page up front so the producer's hot path never pays
        // first-time page-commit cost during measurement / timing.
        memset( mBuffer_, 0, cap * sizeof( QueuePacketLite ) );
    }
}

RingBuffer::~RingBuffer()
{
    if( mBuffer_ ) tracy::tracy_free( mBuffer_ );
    mBuffer_ = nullptr;
    mCur_ = nullptr;
    mEnd_ = nullptr;
    mDeqCur_ = nullptr;
}

RingBuffer::RingBuffer( RingBuffer&& other ) noexcept
    : mBuffer_( other.mBuffer_ )
    , mCur_( other.mCur_ )
    , mEnd_( other.mEnd_ )
    , mDeqCur_( other.mDeqCur_ )
    , mDropped_( other.mDropped_ )
{
    other.mBuffer_ = nullptr;
    other.mCur_ = nullptr;
    other.mEnd_ = nullptr;
    other.mDeqCur_ = nullptr;
    other.mDropped_ = 0;
}

RingBuffer& RingBuffer::operator=( RingBuffer&& other ) noexcept
{
    if( this == &other ) return *this;

    if( mBuffer_ ) tracy::tracy_free( mBuffer_ );

    mBuffer_ = other.mBuffer_;
    mCur_ = other.mCur_;
    mEnd_ = other.mEnd_;
    mDeqCur_ = other.mDeqCur_;
    mDropped_ = other.mDropped_;

    other.mBuffer_ = nullptr;
    other.mCur_ = nullptr;
    other.mEnd_ = nullptr;
    other.mDeqCur_ = nullptr;
    other.mDropped_ = 0;

    return *this;
}

bool RingBuffer::TryDequeue( QueuePacketLite& packet )
{
    if( mDeqCur_ >= mCur_ ) return false;

    packet = *mDeqCur_;
    ++mDeqCur_;
    return true;
}

size_t RingBuffer::GetSize() const
{
    return static_cast<size_t>( mCur_ - mDeqCur_ );
}

StringTable::StringTable()
{
    mStrings_.reserve( 256 );
    mStringMap_.reserve( 256 );
    mPtrMap_.reserve( 256 );
}

StringRef StringTable::Intern(const char* str)
{
    if( !str ) return Intern( std::string() );

    if( str == mLastPtr_ ) return { mLastPtrIdx_ };

    const auto pit = mPtrMap_.find( str );
    if( pit != mPtrMap_.end() )
    {
        mLastPtr_ = str;
        mLastPtrIdx_ = pit->second;
        return { pit->second };
    }

    // mStringMap_ is unordered_map<std::string, ...> — its find() requires
    // a std::string argument (no heterogeneous lookup before C++20).
    // We construct one string here and move it into mStrings_ on miss,
    // so the insert path has only ONE heap allocation total.
    std::string key( str );
    const auto it = mStringMap_.find( key );
    if( it != mStringMap_.end() )
    {
        mPtrMap_[str] = it->second;
        mLastPtr_ = str;
        mLastPtrIdx_ = it->second;
        return { it->second };
    }

    const auto idx = static_cast<uint32_t>(mStrings_.size());
    mStrings_.push_back( std::move( key ) );
    mStringMap_[mStrings_.back()] = idx;
    mPtrMap_[str] = idx;
    mLastPtr_ = str;
    mLastPtrIdx_ = idx;
    return { idx };
}

StringRef StringTable::Intern(const std::string& str)
{
    const auto it = mStringMap_.find(str);
    if (it != mStringMap_.end()) return { it->second };

    const auto idx = static_cast<uint32_t>(mStrings_.size());
    mStrings_.push_back(str);
    mStringMap_[str] = idx;
    return { idx };
}

const char* StringTable::Get( const uint32_t idx) const
{
    if (idx >= mStrings_.size()) return "<invalid>";
    return mStrings_[idx].c_str();
}

std::atomic<Collector::ThreadState*> Collector::sThreadStateHead_ { nullptr };
std::atomic<Collector::ThreadState*> Collector::sRetiredThreadStateHead_ { nullptr };
size_t Collector::sDefaultBufferSize_ = static_cast<size_t>( 8 ) * 1024 * 1024;
thread_local Collector::ThreadState* Collector::sTlsState_ = nullptr;

namespace
{
 // __declspec(thread) on MSVC uses the TLS segment directly (zero-overhead,
// no hidden guard/init calls).  C++11 thread_local on MSVC may call
// __Init_thread_header which itself can allocate, causing infinite recursion
// when global operator new/delete is hooked.
#ifdef _MSC_VER
__declspec(thread) int sMemRecurse = 0;
#else
thread_local int sMemRecurse = 0;
#endif
}

Collector& Collector::Instance()
{
    static Collector instance;
    return instance;
}

Collector::Collector() = default;

Collector::~Collector()
{
    auto* retired = sRetiredThreadStateHead_.exchange( nullptr, std::memory_order_acq_rel );
    while( retired )
    {
        auto* next = retired->next_;
        delete retired;
        retired = next;
    }

    const auto* state = sThreadStateHead_.load( std::memory_order_acquire );
    while( state )
    {
        const auto* next = state->next_;
        delete state;
        state = next;
    }
    sThreadStateHead_.store( nullptr, std::memory_order_release );
}

void Collector::Initialize( const size_t bufferSize)
{
    if( bufferSize > 0 ) sDefaultBufferSize_ = bufferSize;
    WarmupTimestampConverter();
}

Collector::ThreadState& Collector::InitThreadState()
{
    const auto state = new ThreadState( sDefaultBufferSize_ );
    auto head = sThreadStateHead_.load( std::memory_order_relaxed );
    do
    {
        state->next_ = head;
    }
    while( !sThreadStateHead_.compare_exchange_weak( head, state, std::memory_order_release, std::memory_order_relaxed ) );

    sTlsState_ = state;

    // Thread-local guard: when this thread exits, flag its ThreadState as
    // orphaned so the next DrainAllPackets() call can reclaim its memory.
    // Lives in the TLS area so its destructor runs at thread teardown.
    struct TlsExitGuard
    {
        ThreadState* state_ = nullptr;
        ~TlsExitGuard()
        {
            if( state_ ) state_->orphaned_.store( true, std::memory_order_release );
        }
    };
    static thread_local TlsExitGuard sExitGuard;
    sExitGuard.state_ = state;

    return *state;
}

Collector::ThreadState& Collector::InitThreadStateCold()
{
    return InitThreadState();
}

uint32_t Collector::GetThreadId()
{
    static thread_local uint32_t tid = tracy::detail::GetThreadHandleImpl();
    return tid;
}

void Collector::Instant(const char* name, const char* scope)
{
    auto& state = GetThreadState();
    auto* slot = state.buffer_.TryAllocSlot();
    if( !slot ) return;
    slot->tag_ = QueueTypeLite::kInstant;
    slot->timestamp_ = tracy::Profiler::GetTime();
    slot->threadId_ = state.threadId_;
    slot->payload_.instant_.nameRef_ = state.stringTable_.Intern(name);
    slot->payload_.instant_.scopeRef_ = state.stringTable_.Intern(scope);
}

void Collector::Counter(const char* name, const int64_t value)
{
    auto& state = GetThreadState();
    auto* slot = state.buffer_.TryAllocSlot();
    if( !slot ) return;
    slot->tag_ = QueueTypeLite::kCounter;
    slot->timestamp_ = tracy::Profiler::GetTime();
    slot->threadId_ = state.threadId_;
    slot->payload_.counter_.nameRef_ = state.stringTable_.Intern(name);
    slot->payload_.counter_.value_ = value;
}

void Collector::Counter(const char* name, const double value)
{
    auto& state = GetThreadState();
    auto* slot = state.buffer_.TryAllocSlot();
    if( !slot ) return;
    slot->tag_ = QueueTypeLite::kCounterDouble;
    slot->timestamp_ = tracy::Profiler::GetTime();
    slot->threadId_ = state.threadId_;
    slot->payload_.counterDouble_.nameRef_ = state.stringTable_.Intern(name);
    slot->payload_.counterDouble_.value_ = value;
}

void Collector::MemAlloc(const void* ptr, const size_t size) noexcept
{
    if( sMemRecurse ) return;
    ++sMemRecurse;
    auto& state = GetThreadState();
    auto* slot = state.buffer_.TryAllocSlot();
    if( slot )
    {
        slot->tag_ = QueueTypeLite::kMemAlloc;
        slot->timestamp_ = tracy::Profiler::GetTime();
        slot->threadId_ = state.threadId_;
        slot->payload_.memAlloc_.ptr_ = reinterpret_cast<uint64_t>( ptr );
        slot->payload_.memAlloc_.size_ = static_cast<uint64_t>( size );
    }
    --sMemRecurse;
}

void Collector::MemFree(const void* ptr) noexcept
{
    if( sMemRecurse ) return;
    ++sMemRecurse;
    auto& state = GetThreadState();
    auto* slot = state.buffer_.TryAllocSlot();
    if( slot )
    {
        slot->tag_ = QueueTypeLite::kMemFree;
        slot->timestamp_ = tracy::Profiler::GetTime();
        slot->threadId_ = state.threadId_;
        slot->payload_.memFree_.ptr_ = reinterpret_cast<uint64_t>( ptr );
    }
    --sMemRecurse;
}

void Collector::DrainAllPackets( std::vector<DrainPacketView>& out )
{
    // SPSC visibility: producers write to their RingBuffer with plain stores
    // for speed (see RingBuffer comment in the header). Drain may run on a
    // different thread, so synchronise-with the producers' last writes via
    // an acquire fence. The corresponding release is the producers stopping
    // (e.g. thread join, condition variable, or AutoDumpInit shutdown), all
    // of which are at minimum release-ordered.
    std::atomic_thread_fence( std::memory_order_acquire );

    // Reclaim states retired by the previous DrainAllPackets() call.
    // This one-drain grace period keeps StringTable pointers embedded in
    // current DrainPacketView entries valid for exporters that consume |out|
    // after this function returns.
    auto* retired = sRetiredThreadStateHead_.exchange( nullptr, std::memory_order_acq_rel );
    while( retired )
    {
        auto* next = retired->next_;
        delete retired;
        retired = next;
    }

    auto* state = sThreadStateHead_.load( std::memory_order_acquire );
    while( state )
    {
        QueuePacketLite packet{};
        while( state->buffer_.TryDequeue( packet ) )
        {
            // Back-fill threadId_ here for ZoneBegin/ZoneEnd packets, whose
            // hot path intentionally skips writing it (see Collector::ZoneBegin
            // in the header). Other packet kinds set threadId_ at enqueue time;
            // overwriting it with the same owning thread's id is a no-op.
            packet.threadId_ = state->threadId_;
            out.push_back( {packet, &state->stringTable_} );
        }
        state = state->next_;
    }

    // Reclaim orphaned ThreadStates whose owning threads have exited.
    // Producers only ever push new nodes at the head with CAS; they never
    // touch next_ of existing nodes. Drain is single-threaded by contract.
    // We detach orphaned nodes from the active list now, but retire (not
    // delete) them until next drain to avoid invalidating stringTable_ pointers
    // already copied into |out|.
    ThreadState* retiredHead = nullptr;
    ThreadState* prev = nullptr;
    auto* cur = sThreadStateHead_.load( std::memory_order_acquire );
    while( cur )
    {
        auto* const next = cur->next_;
        if( cur->orphaned_.load( std::memory_order_acquire ) )
        {
            if( prev == nullptr )
            {
                // Head case: a concurrent producer may push a new node in
                // front of cur. CAS to splice; if it fails, restart the walk.
                auto* expected = cur;
                if( sThreadStateHead_.compare_exchange_strong(
                        expected, next,
                        std::memory_order_acq_rel, std::memory_order_acquire ) )
                {
                    cur->next_ = retiredHead;
                    retiredHead = cur;
                    cur = next;  // prev stays nullptr (still at head).
                    continue;
                }
                // CAS failed: head moved. Restart from new head.
                prev = nullptr;
                cur = sThreadStateHead_.load( std::memory_order_acquire );
                continue;
            }
            // Interior node: only this drain writes prev->next_, no race.
            prev->next_ = next;
            cur->next_ = retiredHead;
            retiredHead = cur;
            cur = next;
            continue;
        }
        prev = cur;
        cur = next;
    }

    if( retiredHead )
    {
        auto* oldRetiredHead = sRetiredThreadStateHead_.load( std::memory_order_acquire );
        do
        {
            ThreadState* tail = retiredHead;
            while( tail->next_ ) tail = tail->next_;
            tail->next_ = oldRetiredHead;
        }
        while( !sRetiredThreadStateHead_.compare_exchange_weak(
            oldRetiredHead,
            retiredHead,
            std::memory_order_release,
            std::memory_order_acquire ) );
    }
}

const uint8_t* Collector::GetBuffer() { return nullptr; }
size_t Collector::GetBufferSize()
{
    size_t total = 0;
    const auto* state = sThreadStateHead_.load( std::memory_order_acquire );
    while( state )
    {
        total += state->buffer_.GetSize();
        state = state->next_;
    }
    return total;
}

size_t Collector::GetDroppedCount()
{
    size_t dropped = 0;
    const auto* state = sThreadStateHead_.load( std::memory_order_acquire );
    while( state )
    {
        dropped += state->buffer_.GetDroppedCount();
        state = state->next_;
    }
    return dropped;
}

namespace
{

struct TimestampConverter
{
    static const TimestampConverter& Instance()
    {
        static const TimestampConverter converter = Create();
        return converter;
    }

    uint64_t RawToNs( const uint64_t raw ) const
    {
        if( mRawIsNs_ ) return raw;
        return static_cast<uint64_t>( static_cast<double>( raw ) * mNsPerTick_ );
    }

    double RawToUs( const uint64_t raw ) const
    {
        return static_cast<double>( RawToNs( raw ) ) / 1000.0;
    }

private:
    static TimestampConverter Create()
    {
#if defined( _WIN32 ) && defined( TRACY_TIMER_QPC )
        const auto frequency = tracy::GetFrequencyQpc();
        if( frequency > 0 ) return TimestampConverter( false, 1000000000.0 / static_cast<double>( frequency ) );
#endif

#ifndef TRACY_HW_TIMER
        return TimestampConverter( true, 1.0 );
#elif defined( TRACY_TIMER_FALLBACK )
        if( !tracy::HardwareSupportsInvariantTSC() ) return TimestampConverter( true, 1.0 );
#endif

        const auto t0 = std::chrono::high_resolution_clock::now();
        const auto r0 = tracy::Profiler::GetTime();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto r1 = tracy::Profiler::GetTime();

        const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>( t1 - t0 ).count();
        const auto dr = r1 - r0;
        if( dt <= 0 || dr <= 0 ) return TimestampConverter{ true, 1.0 };
        return TimestampConverter{ false, static_cast<double>( dt ) / static_cast<double>( dr ) };
    }

    TimestampConverter( const bool rawIsNs, const double nsPerTick )
        : mNsPerTick_( nsPerTick )
        , mRawIsNs_( rawIsNs )
    {
    }

    double mNsPerTick_;
    bool mRawIsNs_;
    char padding_[7] = {};  // NOLINT(clang-diagnostic-unused-private-field)
};

void WarmupTimestampConverter()
{
    (void)TimestampConverter::Instance();
}

} // namespace

namespace detail {
uint64_t RawTimestampToNs( const uint64_t raw )
{
    return TimestampConverter::Instance().RawToNs( raw );
}
} // namespace detail

DumpManager& DumpManager::Instance()
{
    static DumpManager instance;
    return instance;
}

DumpManager::~DumpManager() noexcept
{
    try
    {
        Shutdown();
    }
    catch( const std::exception& e )
    {
        LogException( "Exception caught during DumpManager shutdown", e.what() );
    }
    catch( ... )
    {
        LogException( "Unknown exception caught during DumpManager shutdown" );
    }
}

void DumpManager::Initialize(const DumpConfig& config)
{
    std::lock_guard<std::mutex> lock( mMtx_ );
    if( mInitialized_ ) return;
    mInitialized_ = true;
    mConfig_ = config;
}

void DumpManager::Shutdown()
{
    bool exportOnDestroy = false;
    {
        std::lock_guard<std::mutex> lock( mMtx_ );
        if( !mInitialized_ ) return;
        mInitialized_ = false;
        exportOnDestroy = mConfig_.exportOnDestroy_;
    }

    // Avoid a second export during process teardown when an explicit export
    // already happened in user code (common in tests). A duplicate export at
    // DLL unload time can race with other static teardown paths.
    if( exportOnDestroy && mExportCount_.load( std::memory_order_relaxed ) == 0 )
    {
        ExportNowOnce();
    }
}

void DumpManager::ExportNowOnce()
{
    std::call_once( mExportOnce_, [this]() {
        try { (void)ExportNow(); }
        catch( const std::exception& e ) { LogException( "Exception caught during ExportNow", e.what() ); }
        catch( ... ) { LogException( "Unknown exception caught during ExportNow" ); }
    } );
}

bool DumpManager::ExportNow()
{
    try
    {
        std::string pid;
#ifdef __linux__
        pid = std::to_string( getpid() );
#else
        pid = std::to_string( GetCurrentProcessId() );
#endif

        const std::string filepath = GetAscendProcessLogPath() + "tracylite_" + pid + ".perfetto-trace";
        return ExportAsPerfetto( filepath.c_str() );
    }
    catch( const std::exception& e )
    {
        LogException( "ExportNow failed", e.what() );
        return false;
    }
    catch( ... )
    {
        LogException( "ExportNow failed with unknown exception" );
        return false;
    }
}

void DumpManager::SetPreExportCallback( const PreExportCallback& cb)
{
    std::lock_guard<std::mutex> lock( mMtx_ );
    mPreExportCb_ = cb;
}

void DumpManager::SetPostExportCallback( const PostExportCallback& cb)
{
    std::lock_guard<std::mutex> lock( mMtx_ );
    mPostExportCb_ = cb;
}

void DumpManager::UpdateConfig(const DumpConfig& config)
{
    std::lock_guard<std::mutex> lock( mMtx_ );
    mConfig_ = config;
}

bool DumpManager::ExportAsPerfetto(const char* filepath)
{
    try
    {
        // Snapshot callbacks under the lock so concurrent setters don't race
        // with the actual invocation, but invoke them without the lock held
        // to avoid running user code under our internal mutex.
        PreExportCallback preCb;
        PostExportCallback postCb;
        {
            std::lock_guard<std::mutex> lock( mMtx_ );
            preCb = mPreExportCb_;
            postCb = mPostExportCb_;
        }

        if( preCb ) preCb();

        std::string path;
        if( filepath && *filepath )
        {
            path = filepath;
        }
        else
        {
            std::string pid;
#ifdef __linux__
            pid = std::to_string( getpid() );
#else
            pid = std::to_string( GetCurrentProcessId() );
#endif
            path = GetAscendProcessLogPath() + "tracylite_" + pid + ".perfetto-trace";
        }

        if( !PerfettoNativeExporter::ExportToFile( Collector::Instance(), path.c_str() ) ) return false;

        {
            std::lock_guard<std::mutex> lock( mMtx_ );
            mLastExportTime_ = std::chrono::system_clock::now();
        }
        mExportCount_.fetch_add( 1, std::memory_order_relaxed );
        if( postCb ) postCb( path );
        return true;
    }
    catch( const std::exception& e )
    {
        LogException( "ExportAsPerfetto failed", e.what() );
        return false;
    }
    catch( ... )
    {
        LogException( "ExportAsPerfetto failed with unknown exception" );
        return false;
    }
}

DumpManager::Stats DumpManager::GetStats() const
{
    Stats stats;
    stats.exportCount_ = mExportCount_.load( std::memory_order_relaxed );
    {
        std::lock_guard<std::mutex> lock( mMtx_ );
        stats.lastExportTime_ = mLastExportTime_;
    }
    stats.eventCount_ = 0;
    stats.bufferSize_ = tracylite::Collector::GetBufferSize();
    return stats;
}

namespace
{
    std::once_flag g_autoDumpOnce;
    AutoDumpInit* g_autoDumpInstance = nullptr;
}

AutoDumpInit& AutoDumpInit::Instance(const DumpConfig& config)
{
    std::call_once( g_autoDumpOnce, [&config]() { g_autoDumpInstance = new AutoDumpInit(config); });
    return *g_autoDumpInstance;
}

AutoDumpInit::AutoDumpInit(const DumpConfig& config)
{
    static std::once_flag collectorOnce;
    std::call_once(collectorOnce, [&config]() {
        const size_t bufferSize = (config.maxBufferMb_ > 0) ? (config.maxBufferMb_ * 1024 * 1024) : (static_cast<size_t>( 64 ) * 1024 * 1024);
        Collector::Initialize(bufferSize);
    });

    DumpManager::Instance().Initialize(config);
}

AutoDumpInit::~AutoDumpInit() noexcept
{
    try { DumpManager::Instance().Shutdown(); }
    catch( const std::exception& e )
    {
        LogException( "Exception caught during AutoDumpInit shutdown", e.what() );
    }
    catch( ... )
    {
        LogException( "Unknown exception caught during AutoDumpInit shutdown" );
    }
}

bool AutoDumpInit::Export(const std::string& /*filepath*/)
{
    return DumpManager::Instance().ExportNow();
}

} // namespace tracylite
