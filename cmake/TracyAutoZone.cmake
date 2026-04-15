# TracyAutoZone.cmake
#
# Provides tracy_auto_zone(<target> [OPTIONS]) which sets up build-time
# injection of ZoneScoped; into every function body of a target's sources.
#
# This replaces the /Gh + _penter approach with zero-overhead compile-time
# source locations.  The original sources are NEVER modified; instrumented
# copies are generated in ${CMAKE_CURRENT_BINARY_DIR}/_tracy_gen/.
#
# Usage:
#
#   include(cmake/TracyAutoZone.cmake)
#
#   add_executable(MyApp main.cpp engine.cpp utils.cpp)
#   target_link_libraries(MyApp PRIVATE Tracy::TracyClient)
#   tracy_auto_zone(MyApp
#       SKIP_FUNCTIONS  main operator==
#       MIN_LINES       2
#       EXCLUDE         utils.cpp          # don't instrument this file
#   )
#
# What it does:
#   1. For each .cpp source of <target> (not in EXCLUDE):
#      - Adds a custom command that runs tracy_inject_zones.py to produce
#        an instrumented copy under _tracy_gen/.
#      - Replaces that source in the target with the generated copy.
#   2. EXCLUDE'd sources remain compiled from their original location.
#   3. The TracyHcomm.hpp header is added automatically.

function(tracy_auto_zone TARGET)
    cmake_parse_arguments(
        PARSE_ARGV 1
        _AZ                         # prefix
        ""                          # options (flags)
        "MIN_LINES;MACRO"           # one-value keywords
        "SKIP_FUNCTIONS;EXCLUDE"    # multi-value keywords
    )

    # Defaults
    if(NOT DEFINED _AZ_MIN_LINES)
        set(_AZ_MIN_LINES 1)
    endif()
    if(NOT DEFINED _AZ_MACRO)
        set(_AZ_MACRO "ZoneScoped;")
    endif()

    # Locate the injector script relative to this helper file
    set(_INJECTOR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/tracy_inject_zones.py")
    cmake_path(ABSOLUTE_PATH _INJECTOR NORMALIZE)

    if(NOT EXISTS "${_INJECTOR}")
        message(FATAL_ERROR "tracy_auto_zone: injector script not found: ${_INJECTOR}")
    endif()

    # Find python
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_FOUND)
        find_program(_PYTHON NAMES python3 python)
        if(NOT _PYTHON)
            message(FATAL_ERROR "tracy_auto_zone: Python 3 interpreter not found")
        endif()
    else()
        set(_PYTHON "${Python3_EXECUTABLE}")
    endif()

    # Build skip-functions argument
    set(_SKIP_ARG "")
    if(_AZ_SKIP_FUNCTIONS)
        list(JOIN _AZ_SKIP_FUNCTIONS "," _SKIP_CSV)
        set(_SKIP_ARG "--skip=${_SKIP_CSV}")
    endif()

    # Build exclude set for fast lookup
    set(_EXCLUDE_SET "")
    foreach(_exc IN LISTS _AZ_EXCLUDE)
        cmake_path(ABSOLUTE_PATH _exc BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        list(APPEND _EXCLUDE_SET "${_exc}")
    endforeach()

    # Generation output directory
    set(_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/_tracy_gen")

    # Get target sources
    get_target_property(_SOURCES ${TARGET} SOURCES)
    if(NOT _SOURCES)
        message(WARNING "tracy_auto_zone: target ${TARGET} has no sources")
        return()
    endif()

    set(_NEW_SOURCES "")

    foreach(_src IN LISTS _SOURCES)
        # Only process .cpp/.cxx/.cc files
        cmake_path(GET _src EXTENSION _ext)
        if(NOT _ext MATCHES "\\.(cpp|cxx|cc|c\\+\\+)$")
            list(APPEND _NEW_SOURCES "${_src}")
            continue()
        endif()

        # Resolve absolute path
        cmake_path(ABSOLUTE_PATH _src BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE _abs_src NORMALIZE)

        # Check exclusion
        if(_abs_src IN_LIST _EXCLUDE_SET)
            list(APPEND _NEW_SOURCES "${_src}")
            continue()
        endif()

        # Determine output path
        cmake_path(GET _src FILENAME _fname)
        set(_out "${_GEN_DIR}/${_fname}")

        # Custom command: input → instrumented output
        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND "${_PYTHON}" "${_INJECTOR}"
                    "${_abs_src}"
                    -o "${_out}"
                    --min-lines ${_AZ_MIN_LINES}
                    --macro "${_AZ_MACRO}"
                    ${_SKIP_ARG}
            DEPENDS "${_abs_src}" "${_INJECTOR}"
            COMMENT "[TracyAutoZone] Injecting ZoneScoped into ${_fname}"
            VERBATIM
        )

        list(APPEND _NEW_SOURCES "${_out}")
    endforeach()

    # Replace sources on target
    set_target_properties(${TARGET} PROPERTIES SOURCES "${_NEW_SOURCES}")

    # Ensure the generated directory is an include path (for relative includes)
    target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

endfunction()
