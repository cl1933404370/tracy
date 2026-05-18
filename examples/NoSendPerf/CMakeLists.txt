cmake_minimum_required(VERSION 3.10)

project(NoSendPerf)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter)

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
    target_link_libraries(TracyNoSendPerfRunner PRIVATE msvcrt vcruntime ucrt msvcprt)
    set_property(TARGET TracyNoSendPerfRunner PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(BUILD_TESTING)
    add_test(NAME TracyNoSendPerf.ZoneBeginEnd
             COMMAND TracyNoSendPerfRunner zone)
    add_test(NAME TracyNoSendPerf.ExportSplit
             COMMAND TracyNoSendPerfRunner export)
    add_test(NAME TracyNoSendPerf.ChunkSelfTest
             COMMAND TracyNoSendPerfRunner chunk)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME TracyNoSendPerf.ReconstructPrefixedE2E
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_LIST_DIR}/tracylite_reconstruct_prefixed_e2e.py
                    ${TracyRootPath}
        )
    endif()
    set_tests_properties(TracyNoSendPerf.ZoneBeginEnd TracyNoSendPerf.ExportSplit TracyNoSendPerf.ChunkSelfTest
                         PROPERTIES LABELS "NoSendPerf;Performance")
    if(Python3_Interpreter_FOUND)
        set_tests_properties(TracyNoSendPerf.ReconstructPrefixedE2E
                             PROPERTIES LABELS "NoSendPerf;Performance")
    endif()
endif()

# ── TracyHcom bundle smoke-test ───────────────────────────────────────────────
# Two targets:
#   TracyHcomCoreOnly – core + chunk transport only (no Perfetto, no third-party SDK dep)
#                       Used to validate Route A bundle wiring in isolation.
#   TracyHcomCase     – full bundle with Perfetto enabled (requires perfetto_sdk).
set(TRACY_HCOM_DIR "${TracyRootPath}/tracy_hcom")
include("${TRACY_HCOM_DIR}/TracyHcomBundle.cmake")

# ── Core-only target (no Perfetto, no third-party SDK) ────────────────────────
add_library(TracyHcomCoreOnly SHARED
    TracyNoSendPerfCase.cpp
)
tracyhcom_embed(TracyHcomCoreOnly)   # no PERFETTO flag
target_compile_definitions(TracyHcomCoreOnly PRIVATE USE_TRACY_HCOM_BUNDLE=1)
target_compile_features(TracyHcomCoreOnly PRIVATE cxx_std_14)
target_link_libraries(TracyHcomCoreOnly PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(TracyHcomCoreOnly PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyHcomCoreOnly PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyHcomCoreOnly PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

add_executable(TracyHcomCoreRunner TracyNoSendPerfRunner.cpp)
target_compile_features(TracyHcomCoreRunner PRIVATE cxx_std_14)
target_link_libraries(TracyHcomCoreRunner PRIVATE TracyHcomCoreOnly)
if(MSVC)
    target_compile_options(TracyHcomCoreRunner PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyHcomCoreRunner PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyHcomCoreRunner PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(BUILD_TESTING)
    add_test(NAME TracyHcomCore.ZoneBeginEnd COMMAND TracyHcomCoreRunner zone)
    set_tests_properties(TracyHcomCore.ZoneBeginEnd
                         PROPERTIES LABELS "TracyHcom;Performance")
endif()

# ── Full bundle target with Perfetto ──────────────────────────────────────────
add_library(TracyHcomCase SHARED
    TracyNoSendPerfCase.cpp
)
tracyhcom_embed(TracyHcomCase PERFETTO)
target_compile_definitions(TracyHcomCase PRIVATE
    USE_TRACY_HCOM_BUNDLE=1
    TRACYLITE_PERFETTO=1)
target_compile_features(TracyHcomCase PRIVATE cxx_std_14)
target_link_libraries(TracyHcomCase PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# Wire the same Perfetto SDK/offline path used by TracyNoSendPerfCase
if(TARGET perfetto_sdk)
    target_link_libraries(TracyHcomCase PRIVATE perfetto_sdk)
    if(DEFINED _perfetto_sdk_dir)
        target_include_directories(TracyHcomCase PRIVATE "${_perfetto_sdk_dir}")
    elseif(DEFINED perfetto_SOURCE_DIR)
        target_include_directories(TracyHcomCase PRIVATE "${perfetto_SOURCE_DIR}/sdk")
    endif()
else()
    target_sources(TracyHcomCase PRIVATE ${TracyOfflinePerfettoSourcePath})
    target_include_directories(TracyHcomCase PRIVATE ${TracyOfflinePerfettoPath})
endif()

if(MSVC)
    target_compile_options(TracyHcomCase PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyHcomCase PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyHcomCase PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

add_executable(TracyHcomRunner TracyNoSendPerfRunner.cpp)
target_compile_features(TracyHcomRunner PRIVATE cxx_std_14)
target_link_libraries(TracyHcomRunner PRIVATE TracyHcomCase)
if(MSVC)
    target_compile_options(TracyHcomRunner PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyHcomRunner PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyHcomRunner PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(BUILD_TESTING)
    add_test(NAME TracyHcom.ZoneBeginEnd  COMMAND TracyHcomRunner zone)
    add_test(NAME TracyHcom.ExportSplit   COMMAND TracyHcomRunner export)
    add_test(NAME TracyHcom.ChunkSelfTest COMMAND TracyHcomRunner chunk)
    set_tests_properties(TracyHcom.ZoneBeginEnd TracyHcom.ExportSplit TracyHcom.ChunkSelfTest
                         PROPERTIES LABELS "TracyHcom;Performance")
endif()

# ── Macro-leak isolation compile check ───────────────────────────────────────
# Ensure tracyhcom_embed() does not leak Tracy compile definitions into other
# host source files.
add_library(TracyHcomMacroIsolationCase SHARED
    TracyNoSendPerfCase.cpp
    TracyHcomMacroLeakProbe.cpp
)
tracyhcom_embed(TracyHcomMacroIsolationCase)
target_compile_definitions(TracyHcomMacroIsolationCase PRIVATE USE_TRACY_HCOM_BUNDLE=1)
target_compile_features(TracyHcomMacroIsolationCase PRIVATE cxx_std_14)
target_link_libraries(TracyHcomMacroIsolationCase PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(MSVC)
    target_compile_options(TracyHcomMacroIsolationCase PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
        "$<$<CONFIG:Release>:/Ob3>")
    target_link_options(TracyHcomMacroIsolationCase PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    set_property(TARGET TracyHcomMacroIsolationCase PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
