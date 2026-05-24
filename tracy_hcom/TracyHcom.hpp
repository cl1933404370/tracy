#ifndef TRACY_HCOM_HPP
#define TRACY_HCOM_HPP

// TracyHcom.hpp
// Minimal public API header for the Hcom bundle.
//
// External consumers need only include this file.  Everything else is pulled
// in transitively.  The consuming CMake target must supply:
//   - ${TRACY_HCOM_DIR}   – directory containing this file
//   - ${TRACY_ROOT}/public – Tracy repository public/ parent directory
//
// Optional feature guards (define before including, or via CMake):
//   TRACYHCOM_ENABLE_PERFETTO   – enables Perfetto native exporter
//   TRACYHCOM_NO_TRACY_PROFILER – (phase two) use slim time shim instead

#include "TracyHcomConfig.hpp"
#include "TracyHcomApi.h"

// Pull in the full Hcom collector surface.
#include "client/TracyLiteAll.hpp"

#ifdef TRACYLITE_PERFETTO
#  include "client/TracyLitePerfetto.hpp"
#endif

// TracyLiteChunkWriter.hpp is header-only; always available.
#include "client/TracyLiteChunkWriter.hpp"

#endif // TRACY_HCOM_HPP
