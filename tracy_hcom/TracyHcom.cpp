// TracyHcom.cpp
// Single-TU compilation entrypoint for the Hcom bundle.
//
// This file is the ONLY translation unit that needs to be added to a host
// target.  It pulls in all Hcom implementation via #include so the compiler
// can inline aggressively across the entire bundle.
//
// Required include paths for the host target (Route A):
//   1. ${TRACY_HCOM_DIR}   – directory containing this file (for TracyHcom*.hpp)
//   2. ${TRACY_ROOT}/public – Tracy repository public/ directory
//                            (resolves client/ and common/ relative includes)
//
// Optional compile definitions:
//   TRACYHCOM_ENABLE_PERFETTO   – compile Perfetto native exporter
//                                 (also requires perfetto.h on include path)
//   TRACYHCOM_NO_TRACY_PROFILER – (phase two) use slim TracyHcomTime.hpp shim

#include "TracyHcomConfig.hpp"

// ── rpmalloc + Tracy allocator plumbing (required with TRACY_ENABLE) ─────────
// TracyAlloc.hpp calls tracy::rpmalloc/rpfree/InitRpmalloc.
// - rpmalloc/rpfree are provided by tracy_rpmalloc.cpp
// - InitRpmalloc is provided by TracyAlloc.cpp (and uses a few globals that
//   are stubbed in TracyHcomStubs.cpp for phase one)
#include "client/tracy_rpmalloc.cpp"
#include "client/TracyAlloc.cpp"

// ── Core collector ────────────────────────────────────────────────────────────
// TracyLiteAll.cpp already includes TracyLiteAll.hpp at its top.
#include "client/TracyLiteAll.cpp"

// ── Common Tracy helpers ──────────────────────────────────────────────────────
// TracySystem.cpp provides GetThreadHandleImpl() and SetThreadName*; it is a
// separate TU in the upstream tree but safe to #include here because all its
// symbols are in namespace tracy and have no duplicate definitions with
// TracyLiteAll.cpp.
#include "common/TracySystem.cpp"

// ── Minimal stubs for symbols normally provided by TracyProfiler.cpp ─────────
// Provides GetThreadHandle() and GetThreadNameData() without pulling in the
// full Tracy client. Removed in phase two when TracyHcomTime.hpp replaces
// the TracyProfiler.hpp dependency.
#include "TracyHcomStubs.cpp"

// ── Perfetto exporter ─────────────────────────────────────────────────────────
#ifdef TRACYLITE_PERFETTO
#  include "client/TracyLitePerfetto.cpp"
#endif

// TracyLiteChunkWriter.hpp is header-only; included transitively via
// TracyHcom.hpp – no .cpp needed.
