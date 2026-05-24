set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(OpenCL REQUIRED)

add_executable(OpenCLVectorAdd OpenCLVectorAdd.cpp)
target_include_directories(OpenCLVectorAdd PUBLIC ../../public/tracy)
target_compile_definitions(OpenCLVectorAdd PUBLIC TRACY_ENABLE=1)
target_link_libraries(OpenCLVectorAdd PRIVATE Tracy::TracyClient Threads::Threads OpenCL::OpenCL ${CMAKE_DL_LIBS})
