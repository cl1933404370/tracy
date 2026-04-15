cmake_minimum_required(VERSION 3.10)

project(NoSendPerf)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(CTest)

set(TracyRootPath "${CMAKE_CURRENT_LIST_DIR}/../..")
set(TracyPublicPath "${TracyRootPath}/public")
set(TracyClientSourcePath "${TracyPublicPath}/TracyClient.cpp")
set(TracyLitePerfettoSourcePath "${TracyPublicPath}/client/TracyLitePerfetto.cpp")
set(TracyOfflinePerfettoPath "${TracyRootPath}/scripts/perfetto-sdk-offline")
set(TracyOfflinePerfettoSourcePath "${TracyOfflinePerfettoPath}/perfetto.cc")

add_library(TracyNoSendPerfCase SHARED
    TracyNoSendPerfCase.cpp
    ${TracyClientSourcePath}
    ${TracyLitePerfettoSourcePath}
)

target_include_directories(TracyNoSendPerfCase PRIVATE ${TracyPublicPath})
target_compile_features(TracyNoSendPerfCase PRIVATE cxx_std_14)
target_compile_definitions(TracyNoSendPerfCase PRIVATE
    TRACY_ENABLE=1
    TRACY_DELAYED_INIT=1
    TRACY_SAVE_NO_SEND=1
    TRACYLITE_PERFETTO=1
    TRACY_ON_DEMAND=1
    TRACY_NO_BROADCAST=1
    TRACY_ONLY_LOCALHOST=1
    TRACY_NO_SAMPLING=1
    TRACY_NO_CONTEXT_SWITCH=1
)

target_link_libraries(TracyNoSendPerfCase PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(MSVC)
    target_compile_options(TracyNoSendPerfCase PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyNoSendPerfCase PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyNoSendPerfCase PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(TARGET perfetto_sdk)
    target_link_libraries(TracyNoSendPerfCase PRIVATE perfetto_sdk)
    if(DEFINED _perfetto_sdk_dir)
        target_include_directories(TracyNoSendPerfCase PRIVATE "${_perfetto_sdk_dir}")
    elseif(DEFINED perfetto_SOURCE_DIR)
        target_include_directories(TracyNoSendPerfCase PRIVATE "${perfetto_SOURCE_DIR}/sdk")
    endif()
else()
    target_sources(TracyNoSendPerfCase PRIVATE
        ${TracyOfflinePerfettoSourcePath}
    )
    target_include_directories(TracyNoSendPerfCase PRIVATE ${TracyOfflinePerfettoPath})
endif()

if(MSVC)
    set_source_files_properties(${TracyLitePerfettoSourcePath}
        PROPERTIES COMPILE_FLAGS "/std:c++17 /bigobj")
else()
    set_source_files_properties(${TracyLitePerfettoSourcePath}
        PROPERTIES COMPILE_FLAGS "-std=c++17")
endif()

add_executable(TracyNoSendPerfRunner TracyNoSendPerfRunner.cpp)
target_compile_features(TracyNoSendPerfRunner PRIVATE cxx_std_14)
target_link_libraries(TracyNoSendPerfRunner PRIVATE TracyNoSendPerfCase)

if(MSVC)
    target_compile_options(TracyNoSendPerfRunner PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyNoSendPerfRunner PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyNoSendPerfRunner PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(BUILD_TESTING)
    add_test(NAME TracyNoSendPerf.ZoneBeginEnd
             COMMAND TracyNoSendPerfRunner zone)
    add_test(NAME TracyNoSendPerf.ExportSplit
             COMMAND TracyNoSendPerfRunner export)
    set_tests_properties(TracyNoSendPerf.ZoneBeginEnd TracyNoSendPerf.ExportSplit
                         PROPERTIES LABELS "NoSendPerf;Performance")
endif()
