cmake_minimum_required(VERSION 3.10)

project(NcclAlgoCompare)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(TRACY_PUBLIC_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(nccl-algo-compare
        nccl_algo_compare.cpp)

target_compile_features(nccl-algo-compare PRIVATE cxx_std_14)
target_compile_definitions(nccl-algo-compare PRIVATE TRACY_ENABLE=1)

# Link against the central Tracy client library so tracy_dump symbols and
# runtime helpers (rpmalloc, ThreadName, etc.) are available.
target_link_libraries(nccl-algo-compare PRIVATE Threads::Threads Tracy::TracyClient ${CMAKE_DL_LIBS})


if (MSVC)
    target_compile_options(nccl-algo-compare PRIVATE /utf-8)
endif ()
