# ── Tracy examples ────────────────────────────────────────────────────────
# Built when TRACY_BUILD_EXAMPLES=ON from the root CMakeLists.txt
# ──────────────────────────────────────────────────────────────────────────

set(TRACY_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(TRACY_PUBLIC_DIR ${TRACY_ROOT}/public)

if (MSVC)
    add_compile_options(/utf-8)
endif ()
add_compile_definitions(
        TRACY_ENABLE=1
        $<$<BOOL:${TRACY_TIMER_FALLBACK}>:TRACY_TIMER_FALLBACK>
        $<$<BOOL:${TRACY_DISALLOW_HW_TIMER}>:TRACY_DISALLOW_HW_TIMER>
)

add_subdirectory(ChromeExport)
add_subdirectory(NcclAlgoCompare)
find_package(OpenCL QUIET)
if (OpenCL_FOUND)
    add_subdirectory(OpenCLVectorAdd)
endif()

# fibers example (matches: g++ fibers.cpp ../public/TracyClient.cpp -I../public/tracy -DTRACY_ENABLE -DTRACY_FIBERS -lpthread -ldl)
add_executable(fibers-example
        fibers.cpp)
target_include_directories(fibers-example PRIVATE ${TRACY_PUBLIC_DIR} ${TRACY_PUBLIC_DIR}/tracy)
target_compile_features(fibers-example PRIVATE cxx_std_17)
target_compile_definitions(fibers-example PRIVATE TRACY_ENABLE TRACY_FIBERS)
find_package(Threads REQUIRED)
target_link_libraries(fibers-example PRIVATE Tracy::TracyClient Threads::Threads dl)
