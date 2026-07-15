cmake_minimum_required(VERSION 3.10)

project(ChromeExportExample)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/TracyAutoZone.cmake)

add_executable(ChromeExportExample ChromeExportExample.cpp)

# Opt-in: global operator new/delete memory tracking
if(TRACY_SAVE_NO_SEND)
    target_sources(ChromeExportExample PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../TracyLiteMemHook.cpp)
endif()

target_include_directories(ChromeExportExample PRIVATE ../../public)
target_compile_features(ChromeExportExample PRIVATE cxx_std_17)

if(TRACY_SAVE_NO_SEND)
    set(TRACY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
    set(TRACY_HCOM_DIR "${TRACY_ROOT}/tracy_hcom")
    include("${TRACY_HCOM_DIR}/TracyHcomBundle.cmake")

    # ChromeTraceDump() exports a Perfetto trace in HComm mode, so embed the
    # Route A bundle instead of linking the full Tracy client target.
    tracyhcom_embed(ChromeExportExample PERFETTO)
    target_compile_definitions(ChromeExportExample PRIVATE
        TRACY_SAVE_NO_SEND=1
        TRACYLITE_PERFETTO=1)
else()
    target_link_libraries(ChromeExportExample PRIVATE
        Tracy::TracyClient
        Threads::Threads
        ${CMAKE_DL_LIBS})
endif()

if(TRACY_SAVE_NO_SEND)
    tracy_auto_zone(ChromeExportExample EXCLUDE ${CMAKE_CURRENT_LIST_DIR}/../TracyLiteMemHook.cpp)
endif()
