// TracyHcomStubs.cpp
// Minimal symbol stubs that TracyLiteAll.cpp/TracySystem.cpp need but that
// normally come from the full TracyProfiler.cpp in a standard Tracy build.
//
// Phase one: provide only what is required to link the bundle without
// pulling in TracyProfiler.cpp (which brings in the entire Tracy client).
// Phase two: replace TracyProfiler.hpp usage with TracyHcomTime.hpp and
// delete this file entirely.
//
// This file is #included by TracyHcom.cpp – do not add it separately.

#include <atomic>

#include "common/TracyApi.h"
#include "common/TracySystem.hpp"
namespace tracy
{

// ── allocator state globals normally owned by TracyProfiler.cpp ───────────────
// TracyAlloc.cpp declares these as extern and expects TracyProfiler.cpp to own
// storage.  For phase-one Hcom we provide standalone storage here.
std::atomic<int> RpInitDone( 0 );
std::atomic<int> RpInitLock( 0 );
thread_local bool RpThreadInitDone = false;
thread_local bool RpThreadShutdown = false;

// ── Thread name data store ────────────────────────────────────────────────────
// TracySystem.cpp calls GetThreadNameData() to link thread names into a
// process-wide linked list.  Provide a standalone atomic here so the symbol
// resolves without any profiler session state.
namespace {
std::atomic<ThreadNameData*> s_hcomThreadNameDataStore( nullptr );
}

std::atomic<ThreadNameData*>& GetThreadNameData()
{
    return s_hcomThreadNameDataStore;
}

// ── Thread handle ─────────────────────────────────────────────────────────────
// TracyLiteAll.hpp/cpp uses tracy::GetThreadHandle() (not the detail:: variant)
// in a few places when TRACY_ENABLE is defined.  Forward to the platform
// implementation already compiled via TracySystem.cpp.
TRACY_API uint32_t GetThreadHandle()
{
    return detail::GetThreadHandleImpl();
}

TRACY_API bool HardwareSupportsInvariantTSC()
{
    // Phase-one bundle omits TracyProfiler.cpp, which normally provides this.
    // Returning false keeps TracyLite timestamp conversion on the safe path.
    return false;
}

} // namespace tracy

