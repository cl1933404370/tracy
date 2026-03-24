#ifndef __TRACY_CHROME_EXPORT_HPP__
#define __TRACY_CHROME_EXPORT_HPP__

// TracyChromeExport - Lightweight offline tracer that outputs Chrome JSON format
// Reuses Tracy's thread-ID primitive; uses thread-local buffers for lock-free recording.
//
// Usage:
//   #include "tracy/TracyHcomm.hpp"
//
//   void MyFunction() {
//       ChromeZoneScoped;            // automatic function name
//   }
//
//   int main() {
//       // Set callback to receive each JSON event line (print, log, write file, etc.)
//       ChromeSetOutputCallback([](const char* line) { printf("%s\n", line); });
//       ChromeSetThreadName("MainThread");
//       MyFunction();
//       // Flush buffered events through callback (call after threads joined)
//       ChromeFlushToCallback();
//   }
// ── Reuse Tracy's thread-ID primitive ───────────────────────────────────────
#include "../client/TracyChromeExport.hpp"
#include "Tracy.hpp"
// ── Platform timing ─────────────────────────────────────────────────────────
// We intentionally do NOT use tracy::Profiler::GetTime() because on x86 it
// returns RDTSC ticks (not nanoseconds).  Calibrating those ticks requires
// the full Profiler infrastructure and m_timerMul, which is only available
// when TRACY_ENABLE is set and the profiler is connected.
// Instead we use the same monotonic-clock fallback path Tracy itself uses
// on the !TRACY_HW_TIMER / TRACY_TIMER_FALLBACK branch

// ── User-facing macros ──────────────────────────────────────────────────────

#ifndef TRACY_ENABLE
#  define ChromeZoneScoped
#  define ChromeZoneNamed( name )
#  define ChromeFrameMark
#  define ChromeFrameMarkNamed( name )
#  define ChromePlot( name, val )
#  define ChromeSetThreadName( name )
#  define ChromeSetOutputCallback( cb )
#  define ChromeFlushToCallback()
#else
#  ifndef TracyConcat
#    define TracyConcat( x, y ) TracyConcatIndirect( x, y )
#  endif
#  ifndef TracyConcatIndirect
#    define TracyConcatIndirect( x, y ) x##y
#  endif
#  define ChromeConcat2( a, b ) a##b
#  define ChromeConcat( a, b ) ChromeConcat2( a, b )
#  define ChromeZoneScoped \
      chrome_export::ChromeScopedZone ChromeConcat( __chrome_zone_, TracyLine )( TracyFunction, TracyFile, TracyLine )

#  define ChromeZoneNamed( name ) \
      chrome_export::ChromeScopedZone ChromeConcat( __chrome_zone_, TracyLine )( name, TracyFile, TracyLine )

#  define ChromeFrameMark \
      chrome_export::ChromeTracer::Instance().RecordFrame( nullptr )

#  define ChromeFrameMarkNamed( name ) \
      chrome_export::ChromeTracer::Instance().RecordFrame( name )

#  define ChromePlot( name, val ) \
      chrome_export::ChromeTracer::Instance().RecordPlot( name, (double)( val ) )

#  define ChromeSetThreadName( name ) \
      chrome_export::ChromeTracer::Instance().SetThreadName( name )

#  define ChromeSetOutputCallback( cb ) \
      chrome_export::ChromeTracer::Instance().SetOutputCallback( cb )

#  define ChromeFlushToCallback() \
      chrome_export::ChromeTracer::Instance().FlushToCallback()
#endif
#endif // __TRACY_CHROME_EXPORT_HPP__
