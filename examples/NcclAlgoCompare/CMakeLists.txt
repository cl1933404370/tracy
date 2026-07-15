set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(TRACY_PUBLIC_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TracyAutoZone.cmake)

add_executable(nccl-algo-compare
        nccl_algo_compare.cpp)

target_compile_features(nccl-algo-compare PRIVATE cxx_std_14)
target_compile_definitions(nccl-algo-compare PRIVATE TRACY_ENABLE=1)

if(TRACY_SAVE_NO_SEND)
    set(TRACY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
    set(TRACY_HCOM_DIR "${TRACY_ROOT}/tracy_hcom")
    include("${TRACY_HCOM_DIR}/TracyHcomBundle.cmake")

    # This example configures and exports Perfetto traces, so compile the
    # Route A HComm bundle directly into the target instead of TracyClient.
    tracyhcom_embed(nccl-algo-compare PERFETTO)
    target_compile_definitions(nccl-algo-compare PRIVATE
        TRACY_SAVE_NO_SEND=1
        TRACYLITE_PERFETTO=1)
    target_link_libraries(nccl-algo-compare PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
else()
    target_link_libraries(nccl-algo-compare PRIVATE
        Threads::Threads
        Tracy::TracyClient
        ${CMAKE_DL_LIBS})
endif()


if (MSVC)
    target_compile_options(nccl-algo-compare PRIVATE /utf-8)
endif ()

if(TRACY_SAVE_NO_SEND)
    tracy_auto_zone(nccl-algo-compare)
endif()
