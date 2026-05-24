# tracy_hcom (Route A bundle)

Portable Hcom source bundle for direct compilation into a host shared library.

## Documentation navigation
- Index: [`docs/HComm_Doc_Index.md`](../docs/HComm_Doc_Index.md)
- SRSSD archive: [`docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md`](../docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md)

## What this bundle provides
- Core collection (`TracyLiteAll`)
- Perfetto export (`TracyLitePerfetto`)
- Text chunk transport (`TracyLiteChunkWriter`)

## Integration model (Route A)
No mirror tree, no prebuilt Hcom library.

Host project:
1. Set `TRACY_ROOT` to Tracy repo root.
2. Set `TRACY_HCOM_DIR` to `${TRACY_ROOT}/tracy_hcom` (optional; defaults to this path).
3. Include `tracy_hcom/TracyHcomBundle.cmake`.
4. Call `tracyhcom_embed(<your_target> [PERFETTO])`.

## Required include paths
`tracyhcom_embed()` wires:
- `${TRACY_HCOM_DIR}`
- `${TRACY_ROOT}/public`

Perfetto mode also requires `perfetto.h` include path from either:
- online `perfetto_sdk`, or
- offline `scripts/perfetto-sdk-offline`.

## Minimal CMake usage
```cmake
set(TRACY_ROOT "<path-to-tracy>")
set(TRACY_HCOM_DIR "${TRACY_ROOT}/tracy_hcom")
include("${TRACY_HCOM_DIR}/TracyHcomBundle.cmake")

add_library(MySo SHARED ...)
tracyhcom_embed(MySo PERFETTO)  # omit PERFETTO for core-only
```

## Direct integration example (without building full TracyClient target)
`examples/NoSendPerf/CMakeLists.txt` provides direct HComm integration targets:
- `TracyHcomCoreOnly` + `TracyHcomCoreRunner` (core + chunk only)
- `TracyHcomCase` + `TracyHcomRunner` (core + Perfetto + chunk)

Both are built by embedding `tracy_hcom/TracyHcom.cpp` through:
```cmake
include("${TRACY_HCOM_DIR}/TracyHcomBundle.cmake")
tracyhcom_embed(TracyHcomCoreOnly)
tracyhcom_embed(TracyHcomCase PERFETTO)
```

### Example validation command (Windows, x64 VS dev shell)
```powershell
cmake --preset win-all-hcomm-debug
cmake --build --preset all-hcomm-debug-build --target TracyHcomCoreRunner TracyHcomRunner
```

## Current phase-one notes
- Still depends on parts of Tracy client internals (phase two will reduce this).
- `NOMINMAX` is applied for MSVC Perfetto builds to avoid Windows `min/max` macro collisions.

## Open Phase-Two Backlog
- [ ] Replace Lite-path time source calls (`Profiler::GetTime`) with a dedicated Hcom time shim.
- [ ] Move thread handle / thread-name store usage to Hcom-owned implementation.
- [ ] Remove `TracyHcomStubs.cpp` once time/thread symbols are fully owned by Hcom path.
- [ ] Make `TRACYHCOM_NO_TRACY_PROFILER` switch a real no-Profiler build path (not a placeholder macro only).

### Current practical portability status
- Current state is phase-one and not yet a "few-file standalone" implementation.
- For external offline integration, use the generated minimal bundle export path (`scripts/tracy_hcom_export_bundle.py`) instead of copying full Tracy client trees.
- Verified minimal export footprint in this workspace:
  - Core: ~35 files
  - Perfetto: ~37 files

### Phase-two done criteria
- With no-Profiler path enabled, build does not require `TracyProfiler.hpp`.
- Build/link no longer depends on `TracyHcomStubs.cpp` for time/thread critical symbols.
- Both core-only and Perfetto modes compile successfully with the same Route A integration flow.

## Validation status in this workspace
- `TracyHcomCoreRunner` and `TracyHcomRunner` build successfully (MSVC x64 dev shell).
- Existing regression tests remain available:
  - `TracyHcom.ZoneBeginEnd`
  - `TracyHcom.ExportSplit`
  - `TracyHcom.ChunkSelfTest`

## Refactor archive
SRSSD archive document for this refactor:
- `docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md`
