# Copilot Instructions

## Project Guidelines
- For MSVC benchmark or profiling runs in this workspace, use an x64 Visual Studio developer environment before configuring, building, or executing targets.

## Implementation Preferences
- Implement Hcom as portable source files (e.g., `TracyHcom.cpp` and corresponding headers) that can be dropped into another shared library and compile directly there.
- Preserve execution efficiency by building Hcom via direct source compilation in the host project rather than requiring separate prebuilt library linkage.
- Ensure headers expose a minimal, well-documented public interface and avoid project-specific dependencies to maximize portability.
- Prefer a self-contained `tracy_hcom/` bundle directory where `TracyHcom.cpp` is a single-TU entry point and the bundle does not rely on implicit include paths from the main Tracy repository; to minimize source forking, when practical use CMake-level symlinks or direct `target_sources` references to existing Tracy files instead of copying and trimming upstream Tracy source files.
- For the Hcom bundle (`tracy_hcom/`) and phase-one integration strategy, prefer Route A: use `target_sources` + `target_include_directories` (do not generate a mirrored source tree). The host project adds `TracyHcom.cpp` via `target_sources`, supplies `${TRACY_HCOM_DIR}` and `${TRACY_ROOT}/public` as include paths, and lets the compiler resolve existing relative includes inside upstream Tracy files without any generated mirror tree.
