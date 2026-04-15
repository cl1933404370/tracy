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
if(TRACY_SAVE_NO_SEND)
    add_subdirectory(NoSendPerf)
endif()
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

target_link_libraries(fibers-example PRIVATE Tracy::TracyClient Threads::Threads)
if (NOT WIN32)
    target_link_libraries(fibers-example PRIVATE ${CMAKE_DL_LIBS})
endif ()

# Simple perfetto test utility to validate Perfetto/JSON export at runtime
if(TRACY_SAVE_NO_SEND)
    add_executable(PerfettoTest
        PerfettoTest/PerfettoTest.cpp)
    target_include_directories(PerfettoTest PRIVATE ${TRACY_PUBLIC_DIR} ${TRACY_PUBLIC_DIR}/tracy)
    target_compile_features(PerfettoTest PRIVATE cxx_std_17)
    target_link_libraries(PerfettoTest PRIVATE Tracy::TracyClient Threads::Threads ${CMAKE_DL_LIBS})

    # If perfetto SDK was configured into the top-level build, enable native export
    if(TARGET perfetto_sdk)
        target_compile_definitions(PerfettoTest PRIVATE TRACYLITE_PERFETTO=1)
        target_link_libraries(PerfettoTest PRIVATE perfetto_sdk)
        if(DEFINED _perfetto_sdk_dir)
            target_include_directories(PerfettoTest PRIVATE "${_perfetto_sdk_dir}")
        elseif(DEFINED perfetto_SOURCE_DIR)
            target_include_directories(PerfettoTest PRIVATE "${perfetto_SOURCE_DIR}/sdk")
        endif()
    endif()
endif()
