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
target_link_libraries(ChromeExportExample PRIVATE Tracy::TracyClient Threads::Threads ${CMAKE_DL_LIBS})

if(TRACY_SAVE_NO_SEND)
    tracy_auto_zone(ChromeExportExample EXCLUDE ${CMAKE_CURRENT_LIST_DIR}/../TracyLiteMemHook.cpp)
endif()
