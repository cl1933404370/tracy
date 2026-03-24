# ── Tracy examples ────────────────────────────────────────────────────────
# Built when TRACY_BUILD_EXAMPLES=ON from the root CMakeLists.txt
# ──────────────────────────────────────────────────────────────────────────

set(TRACY_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(TRACY_PUBLIC_DIR ${TRACY_ROOT}/public)

# ── ChromeExport example ────────────────────────────────────────────────
add_executable(chrome-export-example
    ChromeExport/ChromeExportExample.cpp
    ${TRACY_PUBLIC_DIR}/TracyClient.cpp)
target_include_directories(chrome-export-example PRIVATE ${TRACY_PUBLIC_DIR})
target_compile_features(chrome-export-example PRIVATE cxx_std_17)
target_compile_definitions(chrome-export-example PUBLIC TRACY_ENABLE=1)
if(MSVC)
    target_compile_options(chrome-export-example PRIVATE /utf-8)
endif()
find_package(Threads REQUIRED)
target_link_libraries(chrome-export-example PRIVATE Threads::Threads)

# ── NcclAlgoCompare example ─────────────────────────────────────────────
add_executable(nccl-algo-compare
    NcclAlgoCompare/nccl_algo_compare.cpp
    ${TRACY_PUBLIC_DIR}/TracyClient.cpp)
target_include_directories(nccl-algo-compare PRIVATE ${TRACY_PUBLIC_DIR})
target_compile_features(nccl-algo-compare PRIVATE cxx_std_14)
target_compile_definitions(nccl-algo-compare PUBLIC TRACY_ENABLE=1)
if(MSVC)
    target_compile_options(nccl-algo-compare PRIVATE /utf-8)
endif()
target_link_libraries(nccl-algo-compare PRIVATE Threads::Threads)
