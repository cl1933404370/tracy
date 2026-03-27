cmake_minimum_required(VERSION 3.10)

project(ChromeExportExample)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(ChromeExportExample ChromeExportExample.cpp)

target_compile_features(ChromeExportExample PRIVATE cxx_std_17)
target_link_libraries(ChromeExportExample PRIVATE Threads::Threads ${CMAKE_DL_LIBS} Tracy::TracyClient)

