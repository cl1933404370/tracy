cmake_minimum_required(VERSION 3.10)

# Run version helper script
include(cmake/version.cmake)

project(Tracy LANGUAGES CXX VERSION ${TRACY_VERSION_STRING})

include(CTest)

file(GENERATE OUTPUT .gitignore CONTENT "*")

if(${BUILD_SHARED_LIBS})
    set(DEFAULT_STATIC OFF)
else()
    set(DEFAULT_STATIC ON)
endif()

option(TRACY_STATIC "Whether to build Tracy as a static library" ${DEFAULT_STATIC})
option(TRACY_Fortran "Build Fortran bindings" OFF)
option(TRACY_LTO "Enable Link-Time optimization" OFF)

if(TRACY_Fortran)
  enable_language(Fortran)
  set(CMAKE_Fortran_VERSION 2003)
endif()

if(TRACY_LTO OR CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED)
    if(NOT LTO_SUPPORTED)
        message(WARNING "LTO is not supported!")
    endif()
else()
    set(LTO_SUPPORTED OFF)
endif()

find_package(Threads REQUIRED)
find_package(rocprofiler-sdk PATHS "/opt/rocm/lib/cmake")

set(TRACY_PUBLIC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/public)

if(LTO_SUPPORTED)
    set(TRACY_VISIBILITY "OBJECT")
elseif(TRACY_STATIC)
    set(TRACY_VISIBILITY "STATIC")
else()
    set(TRACY_VISIBILITY "SHARED")
endif()

add_library(TracyClient ${TRACY_VISIBILITY} "${TRACY_PUBLIC_DIR}/TracyClient.cpp")
target_compile_features(TracyClient PUBLIC cxx_std_11)
set_target_properties(TracyClient PROPERTIES INTERPROCEDURAL_OPTIMIZATION ${LTO_SUPPORTED})
target_include_directories(TracyClient SYSTEM PUBLIC
    $<BUILD_INTERFACE:${TRACY_PUBLIC_DIR}>
    $<INSTALL_INTERFACE:include/tracy>)
target_link_libraries(
    TracyClient
    PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS}
)
if(rocprofiler-sdk_FOUND)
    target_compile_definitions(TracyClient PUBLIC TRACY_ROCPROF)
    target_link_libraries(TracyClient PUBLIC rocprofiler-sdk::rocprofiler-sdk)
endif()

if(TRACY_Fortran)
    add_library(TracyClientF90 ${TRACY_VISIBILITY} "${TRACY_PUBLIC_DIR}/TracyClient.F90")
    target_include_directories(TracyClientF90 PUBLIC
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/tracy>)
    target_link_libraries(
        TracyClientF90
        PUBLIC
            TracyClient
    )
    set_target_properties(TracyClientF90 PROPERTIES Fortran_MODULE_DIRECTORY ${PROJECT_BINARY_DIR}
                                                    INTERPROCEDURAL_OPTIMIZATION ${LTO_SUPPORTED})
endif()

# Public dependency on some libraries required when using Mingw
if(WIN32 AND ${CMAKE_CXX_COMPILER_ID} MATCHES "GNU|Clang")
    target_link_libraries(TracyClient PUBLIC ws2_32 dbghelp)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    find_library(EXECINFO_LIBRARY NAMES execinfo REQUIRED)
    target_link_libraries(TracyClient PUBLIC ${EXECINFO_LIBRARY})
endif()

if(TRACY_LIBUNWIND_BACKTRACE)
    include(FindPkgConfig)
    pkg_check_modules(unwind REQUIRED libunwind)
    target_include_directories(TracyClient INTERFACE ${unwind_INCLUDE_DIRS})
    target_link_libraries(TracyClient INTERFACE ${unwind_LINK_LIBRARIES})
endif()

if(TRACY_DEBUGINFOD)
    include(FindPkgConfig)
    pkg_check_modules(debuginfod REQUIRED libdebuginfod)
    target_include_directories(TracyClient INTERFACE ${debuginfod_INCLUDE_DIRS})
    target_link_libraries(TracyClient INTERFACE ${debuginfod_LINK_LIBRARIES})
endif()

add_library(Tracy::TracyClient ALIAS TracyClient)
if(TRACY_Fortran)
    add_library(Tracy::TracyClient_Fortran ALIAS TracyClientF90)
endif()

macro(set_option option help value)
    option(${option} ${help} ${value})
    if(${option})
        message(STATUS "${option}: ON")
        target_compile_definitions(TracyClient PUBLIC ${option})
    else()
        message(STATUS "${option}: OFF")
    endif()
endmacro()

set_option(TRACY_ENABLE "Enable profiling" ON)
set_option(TRACY_ON_DEMAND "On-demand profiling" OFF)
set_option(TRACY_CALLSTACK "Enforce callstack collection for tracy regions" OFF)
set_option(TRACY_NO_CALLSTACK "Disable all callstack related functionality" OFF)
set_option(TRACY_NO_CALLSTACK_INLINES "Disables the inline functions in callstacks" OFF)
set_option(TRACY_ONLY_LOCALHOST "Only listen on the localhost interface" OFF)
set_option(TRACY_NO_BROADCAST "Disable client discovery by broadcast to local network" OFF)
set_option(TRACY_ONLY_IPV4 "Tracy will only accept connections on IPv4 addresses (disable IPv6)" OFF)
set_option(TRACY_NO_CODE_TRANSFER "Disable collection of source code" OFF)
set_option(TRACY_NO_CONTEXT_SWITCH "Disable capture of context switches" OFF)
set_option(TRACY_NO_EXIT "Client executable does not exit until all profile data is sent to server" OFF)
set_option(TRACY_NO_SAMPLING "Disable call stack sampling" OFF)
set_option(TRACY_NO_VERIFY "Disable zone validation for C API" OFF)
set_option(TRACY_NO_VSYNC_CAPTURE "Disable capture of hardware Vsync events" OFF)
set_option(TRACY_NO_FRAME_IMAGE  "Disable the frame image support and its thread" OFF)
set_option(TRACY_NO_SYSTEM_TRACING  "Disable systrace sampling" OFF)
set_option(TRACY_PATCHABLE_NOPSLEDS  "Enable nopsleds for efficient patching by system-level tools (e.g. rr)" OFF)
set_option(TRACY_DELAYED_INIT "Enable delayed initialization of the library (init on first call)" OFF)
set_option(TRACY_MANUAL_LIFETIME "Enable the manual lifetime management of the profile" OFF)
set_option(TRACY_FIBERS "Enable fibers support" OFF)
set_option(TRACY_NO_CRASH_HANDLER "Disable crash handling" OFF)
set_option(TRACY_TIMER_FALLBACK "Use lower resolution timers" OFF)
set_option(TRACY_LIBUNWIND_BACKTRACE "Use libunwind backtracing where supported" OFF)
set_option(TRACY_SYMBOL_OFFLINE_RESOLVE "Instead of full runtime symbol resolution, only resolve the image path and offset to enable offline symbol resolution" OFF)
set_option(TRACY_LIBBACKTRACE_ELF_DYNLOAD_SUPPORT "Enable libbacktrace to support dynamically loaded elfs in symbol resolution resolution after the first symbol resolve operation" OFF)
set_option(TRACY_DEBUGINFOD "Enable debuginfod support" OFF)
set_option(TRACY_IGNORE_MEMORY_FAULTS "Ignore instrumentation errors from memory free events that do not have a matching allocation" OFF)

# advanced
set_option(TRACY_VERBOSE "[advanced] Verbose output from the profiler" OFF)
mark_as_advanced(TRACY_VERBOSE)
set_option(TRACY_DEMANGLE "[advanced] Don't use default demangling function - You'll need to provide your own" OFF)
mark_as_advanced(TRACY_DEMANGLE)
if(rocprofiler-sdk_FOUND)
    set_option(TRACY_ROCPROF_CALIBRATION "[advanced] Use continuous calibration of the Rocprof GPU time." OFF)
    mark_as_advanced(TRACY_ROCPROF_CALIBRATION)
endif()
set_option(TRACY_SAVE_NO_SEND "Enable TracyLite no-send Perfetto export (TracyHcomm)" OFF)
set_option(TRACYLITE_PERFETTO_COMPACT_EXPORT "Use compact TracyLite Perfetto export (smaller output, fewer debug fields)" OFF)
if (TRACY_SAVE_NO_SEND)
    target_compile_definitions(TracyClient PUBLIC TRACY_SAVE_NO_SEND)

    # ── Perfetto SDK acquisition ─────────────────────────────────────────
    #
    # Strategy (unified for all compilers, including MSVC):
    #   1. Try online: auto-detect latest tag or use user-pinned tag.
    #   2. Download via CPM (git clone):
    #      a. ≤v49: use pre-built sdk/perfetto.cc directly.
    #      b. v50+: try gen_amalgamated (GCC/Clang only, requires Python+GN).
    #   3. If still no SDK, download perfetto-cpp-sdk-src.zip from GitHub
    #      Releases (works on all platforms, no gen_amalgamated needed).
    #   4. Fallback to bundled offline SDK.
    #
    set(TRACYLITE_PERFETTO_TAG "" CACHE STRING
        "Perfetto SDK git tag (empty = auto-detect latest)")
    set(_perfetto_offline_dir "${CMAKE_CURRENT_SOURCE_DIR}/scripts/perfetto-sdk-offline")
    set(_perfetto_sdk_dir "")
    set(_perfetto_version "unknown")

    # ── Helper: use offline SDK ──────────────────────────────────────────
    macro(_use_perfetto_offline)
        if(EXISTS "${_perfetto_offline_dir}/perfetto.cc"
           AND EXISTS "${_perfetto_offline_dir}/perfetto.h")
            set(_perfetto_sdk_dir "${_perfetto_offline_dir}")
            if(EXISTS "${_perfetto_offline_dir}/VERSION")
                file(READ "${_perfetto_offline_dir}/VERSION" _perfetto_version)
                string(STRIP "${_perfetto_version}" _perfetto_version)
            else()
                set(_perfetto_version "v49.0-offline")
            endif()
            message(STATUS "Perfetto SDK: using offline (${_perfetto_version})")
        endif()
    endmacro()

    # ── All compilers: try online, fallback to offline ───────────────────
    set(_resolved_tag "${TRACYLITE_PERFETTO_TAG}")
    set(_network_ok FALSE)

    if(NOT _resolved_tag)
        # Auto-detect latest release from GitHub API (10s timeout).
        set(_api_file "${CMAKE_BINARY_DIR}/_perfetto_latest_release.json")
        file(DOWNLOAD
            "https://api.github.com/repos/google/perfetto/releases/latest"
            "${_api_file}"
            TIMEOUT 10
            STATUS _api_status
        )
        list(GET _api_status 0 _api_rc)
        if(_api_rc EQUAL 0 AND EXISTS "${_api_file}")
            file(READ "${_api_file}" _api_json)
            string(REGEX MATCH "\"tag_name\"[ \t\n]*:[ \t\n]*\"([^\"]+)\"" _tag_match "${_api_json}")
            if(CMAKE_MATCH_1)
                set(_resolved_tag "${CMAKE_MATCH_1}")
                set(_network_ok TRUE)
                message(STATUS "Perfetto SDK: latest release is ${_resolved_tag}")
            endif()
        endif()
        if(NOT _resolved_tag)
            message(STATUS "Perfetto SDK: GitHub API unreachable, skipping online download")
        endif()
    else()
        # User pinned a specific tag; verify network with a quick probe.
        set(_probe_file "${CMAKE_BINARY_DIR}/_perfetto_probe.tmp")
        file(DOWNLOAD
            "https://github.com/google/perfetto/tree/${_resolved_tag}"
            "${_probe_file}"
            TIMEOUT 10
            STATUS _probe_status
        )
        list(GET _probe_status 0 _probe_rc)
        if(_probe_rc EQUAL 0)
            set(_network_ok TRUE)
        else()
            message(STATUS "Perfetto SDK: network probe failed for ${_resolved_tag}, skipping online download")
        endif()
    endif()

    # ── Online download via CPM ──────────────────────────────────────────
    if(_network_ok AND _resolved_tag)
        include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)

        # Set git timeout: abort if < 1 KB/s for 300 seconds.
        set(ENV{GIT_HTTP_LOW_SPEED_LIMIT} "1000")
        set(ENV{GIT_HTTP_LOW_SPEED_TIME} "300")

        CPMAddPackage(
            NAME perfetto
            GITHUB_REPOSITORY google/perfetto
            GIT_TAG ${_resolved_tag}
            GIT_SHALLOW TRUE
            DOWNLOAD_ONLY TRUE
        )

        unset(ENV{GIT_HTTP_LOW_SPEED_LIMIT})
        unset(ENV{GIT_HTTP_LOW_SPEED_TIME})

        if(perfetto_SOURCE_DIR AND EXISTS "${perfetto_SOURCE_DIR}/sdk/perfetto.cc")
            # ≤v49: amalgamated SDK ships in the repo.
            set(_perfetto_sdk_dir "${perfetto_SOURCE_DIR}/sdk")
            set(_perfetto_version "${_resolved_tag}")
            message(STATUS "Perfetto SDK: online ${_resolved_tag} (amalgamated)")
        elseif(perfetto_SOURCE_DIR)
            # v50+: amalgamated SDK not pre-built in the repo.
            # Try gen_amalgamated on non-MSVC platforms (requires Python + GN).
            if(NOT MSVC AND EXISTS "${perfetto_SOURCE_DIR}/tools/gen_amalgamated")
                find_package(Python3 QUIET COMPONENTS Interpreter)
                if(Python3_FOUND)
                    message(STATUS "Perfetto SDK: running gen_amalgamated for ${_resolved_tag}...")
                    execute_process(
                        COMMAND ${Python3_EXECUTABLE}
                            "${perfetto_SOURCE_DIR}/tools/gen_amalgamated"
                            "--output" "${perfetto_SOURCE_DIR}/sdk/perfetto"
                        WORKING_DIRECTORY "${perfetto_SOURCE_DIR}"
                        RESULT_VARIABLE _gen_rc
                        TIMEOUT 120
                        OUTPUT_QUIET ERROR_QUIET
                    )
                    if(_gen_rc EQUAL 0 AND EXISTS "${perfetto_SOURCE_DIR}/sdk/perfetto.cc")
                        set(_perfetto_sdk_dir "${perfetto_SOURCE_DIR}/sdk")
                        set(_perfetto_version "${_resolved_tag}")
                        message(STATUS "Perfetto SDK: online ${_resolved_tag} (generated)")
                    else()
                        message(WARNING "Perfetto SDK: gen_amalgamated failed (rc=${_gen_rc})")
                    endif()
                else()
                    message(WARNING "Perfetto SDK: ${_resolved_tag} needs Python3 for gen_amalgamated")
                endif()
            endif()
        endif()
    endif()

    # ── Fallback: download pre-built SDK zip from GitHub Releases ────────
    # Works on all platforms including MSVC; no gen_amalgamated needed.
    if(NOT _perfetto_sdk_dir AND _network_ok AND _resolved_tag)
        set(_zip_url
            "https://github.com/google/perfetto/releases/download/${_resolved_tag}/perfetto-cpp-sdk-src.zip")
        set(_zip_file "${CMAKE_BINARY_DIR}/_perfetto-cpp-sdk-src.zip")
        set(_zip_extract_dir "${CMAKE_BINARY_DIR}/_perfetto_sdk_zip")
        message(STATUS "Perfetto SDK: trying release zip for ${_resolved_tag}...")
        file(DOWNLOAD "${_zip_url}" "${_zip_file}"
            TIMEOUT 60 STATUS _zip_status)
        list(GET _zip_status 0 _zip_rc)
        if(_zip_rc EQUAL 0 AND EXISTS "${_zip_file}")
            file(MAKE_DIRECTORY "${_zip_extract_dir}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xf "${_zip_file}"
                WORKING_DIRECTORY "${_zip_extract_dir}"
                RESULT_VARIABLE _extract_rc
            )
            if(_extract_rc EQUAL 0)
                # Check common layouts: flat or sdk/ subdirectory.
                if(EXISTS "${_zip_extract_dir}/perfetto.cc"
                   AND EXISTS "${_zip_extract_dir}/perfetto.h")
                    set(_perfetto_sdk_dir "${_zip_extract_dir}")
                    set(_perfetto_version "${_resolved_tag}")
                    message(STATUS "Perfetto SDK: online ${_resolved_tag} (release zip)")
                elseif(EXISTS "${_zip_extract_dir}/sdk/perfetto.cc"
                       AND EXISTS "${_zip_extract_dir}/sdk/perfetto.h")
                    set(_perfetto_sdk_dir "${_zip_extract_dir}/sdk")
                    set(_perfetto_version "${_resolved_tag}")
                    message(STATUS "Perfetto SDK: online ${_resolved_tag} (release zip)")
                else()
                    message(WARNING "Perfetto SDK: release zip extracted but perfetto.cc not found")
                endif()
            else()
                message(WARNING "Perfetto SDK: failed to extract release zip (rc=${_extract_rc})")
            endif()
        else()
            list(GET _zip_status 1 _zip_msg)
            message(STATUS "Perfetto SDK: release zip not available (${_zip_msg})")
        endif()
    endif()

    # ── Automatic offline fallback ───────────────────────────────────────
    if(NOT _perfetto_sdk_dir)
        message(STATUS "Perfetto SDK: online methods exhausted, falling back to offline")
        _use_perfetto_offline()
    endif()

    if(NOT _perfetto_sdk_dir)
        message(FATAL_ERROR
            "TRACY_SAVE_NO_SEND requires Perfetto SDK but all methods failed:\n"
            "  - Online download: ${_resolved_tag} (network_ok=${_network_ok})\n"
            "  - Offline SDK not found at: ${_perfetto_offline_dir}\n"
            "Fix: run  python scripts/download_perfetto_offline.py")
    endif()

    # ── Build perfetto_sdk static library ─────────────────────────────────
    # Use protozero-only subset when available (98% smaller than full SDK).
    # Tracy only needs the protozero serializer; the full Perfetto runtime
    # (tracing service, IPC, threads) is dead code.
    # Generate the subset with: python scripts/extract_protozero.py
    set(_perfetto_protozero "${_perfetto_sdk_dir}/perfetto_protozero.cc")
    if(NOT EXISTS "${_perfetto_protozero}")
        # Auto-extract protozero subset if Python is available.
        find_package(Python3 QUIET COMPONENTS Interpreter)
        set(_extract_script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/extract_protozero.py")
        if(Python3_FOUND AND EXISTS "${_extract_script}" AND EXISTS "${_perfetto_sdk_dir}/perfetto.cc")
            message(STATUS "Perfetto SDK: extracting protozero subset...")
            execute_process(
                COMMAND ${Python3_EXECUTABLE} "${_extract_script}"
                    "--input" "${_perfetto_sdk_dir}/perfetto.cc"
                    "--output" "${_perfetto_protozero}"
                RESULT_VARIABLE _extract_rc
                OUTPUT_QUIET
            )
            if(NOT _extract_rc EQUAL 0)
                message(STATUS "Perfetto SDK: extraction failed (rc=${_extract_rc}), using full source")
            endif()
        endif()
    endif()
    if(EXISTS "${_perfetto_protozero}")
        set(_perfetto_src "${_perfetto_protozero}")
        message(STATUS "Perfetto SDK: using protozero-only subset (fast build)")
    else()
        set(_perfetto_src "${_perfetto_sdk_dir}/perfetto.cc")
        message(STATUS "Perfetto SDK: using full amalgamated source (slow build)")
    endif()
    add_library(perfetto_sdk STATIC "${_perfetto_src}")
    target_include_directories(perfetto_sdk PRIVATE "${_perfetto_sdk_dir}")
    target_compile_features(perfetto_sdk PRIVATE cxx_std_17)
    target_compile_definitions(perfetto_sdk PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_definitions(perfetto_sdk PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
        if(_perfetto_src STREQUAL _perfetto_protozero)
            # ProtoZero subset is small enough that /bigobj is not needed.
            target_compile_options(perfetto_sdk PRIVATE /WX- /wd4065 /wd4127 /wd4146 /wd4244 /wd4267 /wd4369 /wd4459 /wd4819 /wd4996)
        else()
            target_compile_options(perfetto_sdk PRIVATE /WX- /bigobj /wd4065 /wd4127 /wd4146 /wd4244 /wd4267 /wd4369 /wd4459 /wd4819 /wd4996)
        endif()
    endif()
    set_target_properties(perfetto_sdk PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION OFF)

    # ── TracyLitePerfetto.cpp (C++17 TU) ──────────────────────────────────
    target_sources(TracyClient PRIVATE "${TRACY_PUBLIC_DIR}/client/TracyLitePerfetto.cpp")
    if(MSVC)
        set_source_files_properties("${TRACY_PUBLIC_DIR}/client/TracyLitePerfetto.cpp"
            PROPERTIES COMPILE_FLAGS "/std:c++17 /bigobj")
    else()
        set_source_files_properties("${TRACY_PUBLIC_DIR}/client/TracyLitePerfetto.cpp"
            PROPERTIES COMPILE_FLAGS "-std=c++17")
    endif()

    target_compile_definitions(TracyClient PUBLIC TRACYLITE_PERFETTO=1)
    if(TRACYLITE_PERFETTO_COMPACT_EXPORT)
        target_compile_definitions(TracyClient PRIVATE TRACYLITE_PERFETTO_COMPACT_LEVEL=1)
    else()
        target_compile_definitions(TracyClient PRIVATE TRACYLITE_PERFETTO_COMPACT_LEVEL=0)
    endif()
    target_compile_definitions(TracyClient PRIVATE "TRACYLITE_PERFETTO_SDK_VERSION=\"${_perfetto_version}\"")
    target_link_libraries(TracyClient PRIVATE perfetto_sdk)
    target_include_directories(TracyClient PRIVATE "${_perfetto_sdk_dir}")
    message(STATUS "TRACY_SAVE_NO_SEND: ON (Perfetto ${_perfetto_version})")
else ()
    message(STATUS "TRACY_SAVE_NO_SEND: OFF")
endif ()

# handle incompatible combinations
if(TRACY_MANUAL_LIFETIME AND NOT TRACY_DELAYED_INIT)
    message(FATAL_ERROR "TRACY_MANUAL_LIFETIME can not be activated with disabled TRACY_DELAYED_INIT")
endif()

if(NOT TRACY_STATIC)
    target_compile_definitions(TracyClient PRIVATE TRACY_EXPORTS)
    target_compile_definitions(TracyClient PUBLIC TRACY_IMPORTS)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set_target_properties(TracyClient PROPERTIES VERSION ${PROJECT_VERSION})
if(TRACY_Fortran)
    set_target_properties(TracyClientF90 PROPERTIES VERSION ${PROJECT_VERSION})
endif()

set(tracy_includes
    ${TRACY_PUBLIC_DIR}/tracy/TracyC.h
    ${TRACY_PUBLIC_DIR}/tracy/Tracy.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyCUDA.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyD3D11.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyD3D12.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyLua.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyMetal.hmm
    ${TRACY_PUBLIC_DIR}/tracy/TracyOpenCL.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyOpenGL.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyVulkan.hpp
    ${TRACY_PUBLIC_DIR}/tracy/TracyHcomm.hpp)

set(client_includes
    ${TRACY_PUBLIC_DIR}/client/tracy_concurrentqueue.h
    ${TRACY_PUBLIC_DIR}/client/tracy_rpmalloc.hpp
    ${TRACY_PUBLIC_DIR}/client/tracy_SPSCQueue.h
    ${TRACY_PUBLIC_DIR}/client/TracyKCore.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyArmCpuTable.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyCallstack.h
    ${TRACY_PUBLIC_DIR}/client/TracyCallstack.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyCpuid.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyDebug.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyDxt1.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyFastVector.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyLock.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyProfiler.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyRingBuffer.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyScoped.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyStringHelpers.hpp
    ${TRACY_PUBLIC_DIR}/client/TracySysPower.hpp
    ${TRACY_PUBLIC_DIR}/client/TracySysTime.hpp
    ${TRACY_PUBLIC_DIR}/client/TracySysTrace.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyThread.hpp
    ${TRACY_PUBLIC_DIR}/client/TracyLiteAll.hpp)

set(common_includes
    ${TRACY_PUBLIC_DIR}/common/tracy_lz4.hpp
    ${TRACY_PUBLIC_DIR}/common/tracy_lz4hc.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyAlign.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyAlloc.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyApi.h
    ${TRACY_PUBLIC_DIR}/common/TracyColor.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyForceInline.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyMutex.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyProtocol.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyQueue.hpp
    ${TRACY_PUBLIC_DIR}/common/TracySocket.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyStackFrames.hpp
    ${TRACY_PUBLIC_DIR}/common/TracySystem.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyWinFamily.hpp
    ${TRACY_PUBLIC_DIR}/common/TracyYield.hpp)

install(TARGETS TracyClient
        EXPORT TracyConfig
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
        COMPONENT lib)
if(TARGET perfetto_sdk)
    install(TARGETS perfetto_sdk
            EXPORT TracyConfig
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
            COMPONENT lib)
endif()
if(TRACY_Fortran)
    install(TARGETS TracyClientF90
            EXPORT TracyConfig
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>
            COMPONENT lib)
endif()
# Export targets to build tree root
set(_tracy_export_targets TracyClient)
if(TARGET perfetto_sdk)
    list(APPEND _tracy_export_targets perfetto_sdk)
endif()
export(TARGETS ${_tracy_export_targets}
       NAMESPACE Tracy::
       FILE ${CMAKE_BINARY_DIR}/TracyTargets.cmake)
if(TRACY_Fortran)
    export(TARGETS TracyClientF90
           NAMESPACE Tracy::
           APPEND
           FILE ${CMAKE_BINARY_DIR}/TracyTargets.cmake)
endif()
install(FILES ${tracy_includes}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy/tracy)
install(FILES ${client_includes}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy/client)
install(FILES ${common_includes}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy/common)
if(TRACY_Fortran)
    if(${CMAKE_Fortran_COMPILER_ID} MATCHES "Cray")
        install(FILES ${PROJECT_BINARY_DIR}/TRACY.mod
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy)
    else()
        install(FILES ${PROJECT_BINARY_DIR}/tracy.mod
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy)
    endif()
endif()
install(EXPORT TracyConfig
        NAMESPACE Tracy::
        FILE TracyTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/TracyConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/TracyConfig.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

option(TRACY_CLIENT_PYTHON "Whether to build Tracy python client library" OFF)

if(TRACY_CLIENT_PYTHON)
    if(TRACY_STATIC)
        message(FATAL_ERROR "Python-bindings require a shared client library")
    endif()

    add_subdirectory(python)
endif()

# ---------------------------------------------------------------------------
# Optional sub-projects built from this root.
# The client library (TracyClient .a/.lib) is ALWAYS built.
# Enable individual tools with:
#   -DTRACY_BUILD_CAPTURE=ON   tracy-capture / tracy-capture-daemon
#   -DTRACY_BUILD_TEST=ON      tracy-test
#   -DTRACY_BUILD_PROFILER=ON  tracy-profiler  (GUI, requires cmake ≥ 3.25)
#   -DTRACY_BUILD_EXAMPLES=ON  example programs (ChromeExport, etc.)
#   -DTRACY_BUILD_IMPORT=ON    tracy-import-chrome / tracy-import-fuchsia
#   -DTRACY_BUILD_CSVEXPORT=ON tracy-csvexport
#   -DTRACY_BUILD_MERGE=ON     tracy-merge
# ---------------------------------------------------------------------------
option(TRACY_BUILD_CAPTURE  "Build tracy-capture and tracy-capture-daemon"   OFF)
option(TRACY_BUILD_TEST     "Build tracy-test example"                       OFF)
option(TRACY_BUILD_PROFILER "Build tracy-profiler GUI"                       OFF)
option(TRACY_BUILD_EXAMPLES "Build example programs"                         OFF)
option(TRACY_BUILD_IMPORT   "Build tracy-import-chrome / tracy-import-fuchsia" OFF)
option(TRACY_BUILD_CSVEXPORT "Build tracy-csvexport"                         OFF)
option(TRACY_BUILD_MERGE    "Build tracy-merge"                              OFF)

if(TRACY_BUILD_CAPTURE)
    add_subdirectory(capture)
endif()

if(TRACY_BUILD_TEST)
    add_subdirectory(test)
endif()

if(TRACY_BUILD_PROFILER)
    if(CMAKE_VERSION VERSION_LESS "3.25")
        message(FATAL_ERROR "TRACY_BUILD_PROFILER requires CMake 3.25 or newer")
    endif()
    add_subdirectory(profiler)
endif()

if(TRACY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TRACY_BUILD_IMPORT)
    add_subdirectory(import)
endif()

if(TRACY_BUILD_CSVEXPORT)
    add_subdirectory(csvexport)
endif()

if(TRACY_BUILD_MERGE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/merge/CMakeLists.txt")
        add_subdirectory(merge)
    else()
        message(WARNING
            "TRACY_BUILD_MERGE=ON, but ${CMAKE_CURRENT_SOURCE_DIR}/merge does not exist. "
            "Skipping tracy-merge. Disable TRACY_BUILD_MERGE or add the missing merge subproject.")
    endif()
endif()

# Keep CPack at the very end so third-party dependencies can't overwrite
# top-level package metadata used by CMake package presets.
set(CPACK_PACKAGE_NAME "Tracy")
set(CPACK_PACKAGE_VENDOR "Tracy")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)
