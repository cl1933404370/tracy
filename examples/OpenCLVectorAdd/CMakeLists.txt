cmake_minimum_required(VERSION 3.10)

project(OpenCLVectorAdd)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(OpenCL REQUIRED)

add_executable(OpenCLVectorAdd OpenCLVectorAdd.cpp)

# Link against the central Tracy client library provided by the top-level project
target_link_libraries(OpenCLVectorAdd PUBLIC
        OpenCL::OpenCL
        Tracy::TracyClient
        ${CMAKE_DL_LIBS}
        Threads::Threads)

# Allow including <Tracy.hpp>
target_include_directories(OpenCLVectorAdd PRIVATE ../../public/tracy)
