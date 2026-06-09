# TracyHcomBundle.cmake
#
# Reusable CMake helper for embedding the Hcom bundle directly into a host
# target (Route A: direct source compilation, no prebuilt library).
#
# ── Quick-start ──────────────────────────────────────────────────────────────
#
#   # In your CMakeLists.txt:
#   set(TRACY_ROOT  "/path/to/tracy")   # Tracy repository root
#   set(TRACY_HCOM_DIR "${TRACY_ROOT}/tracy_hcom")
#   include("${TRACY_HCOM_DIR}/TracyHcomBundle.cmake")
#
#   add_library(my_so SHARED ...)
#   tracyhcom_embed(my_so)              # wires sources, includes, compile defs
#
#   # Optional: enable Perfetto export
#   tracyhcom_embed(my_so PERFETTO)
#
#   # Perfetto integration (in priority order):
#   #   1) If a perfetto_sdk target exists, it is linked automatically.
#   #   2) Else, use TRACYHCOM_PERFETTO_SDK_DIR if provided.
#   #   3) Else, fallback to ${TRACY_ROOT}/scripts/perfetto-sdk-offline.
#   #
#   # If no Perfetto implementation source is found, configuration fails.
#
# ── Variables set by this file ───────────────────────────────────────────────
#   TRACYHCOM_SOURCE_FILE  – absolute path to TracyHcom.cpp
#   TRACYHCOM_INCLUDE_DIRS – list of required include directories
#   TRACYHCOM_PERFETTO_SDK_DIR (optional)
#     Absolute path to a Perfetto amalgamated SDK directory containing
#     perfetto.h plus either perfetto_protozero.cc or perfetto.cc.
#
# ── Requirements ─────────────────────────────────────────────────────────────
#   TRACY_ROOT     must be set before including this file
#   TRACY_HCOM_DIR must be set before including this file (or it is inferred
#                  as ${TRACY_ROOT}/tracy_hcom automatically)
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.14)

# ── Validate required variables ───────────────────────────────────────────────
if(NOT DEFINED TRACY_ROOT)
	message(FATAL_ERROR
		"[TracyHcomBundle] TRACY_ROOT is not set.\n"
		"Set it to the root of the Tracy repository before including this file.")
endif()

if(NOT DEFINED TRACY_HCOM_DIR)
	set(TRACY_HCOM_DIR "${TRACY_ROOT}/tracy_hcom")
endif()

if(NOT EXISTS "${TRACY_ROOT}/public/client/TracyLiteAll.hpp")
	message(FATAL_ERROR
		"[TracyHcomBundle] TRACY_ROOT (${TRACY_ROOT}) does not look like a valid "
		"Tracy repository root: public/client/TracyLiteAll.hpp not found.")
endif()

# ── Exported variables ────────────────────────────────────────────────────────
set(TRACYHCOM_SOURCE_FILE  "${TRACY_HCOM_DIR}/TracyHcom.cpp")
set(TRACYHCOM_INCLUDE_DIRS
	"${TRACY_HCOM_DIR}"           # resolves TracyHcom*.hpp
	"${TRACY_ROOT}/public"        # resolves client/ and common/ relative paths
)

# ── Helper function ───────────────────────────────────────────────────────────
# tracyhcom_embed(<target> [PERFETTO])
#
#   Adds TracyHcom.cpp to <target> and configures include paths and compile
#   definitions.  Pass PERFETTO to enable the Perfetto native exporter.
#
function(tracyhcom_embed TARGET)
	cmake_parse_arguments(HCOM "PERFETTO" "" "" ${ARGN})

	# Compile TracyHcom.cpp in a private OBJECT target so Tracy-related compile
	# definitions do not leak into other sources of the host target.
	set(_tracyhcom_obj_target "${TARGET}__tracyhcom_obj")
	if(NOT TARGET ${_tracyhcom_obj_target})
		add_library(${_tracyhcom_obj_target} OBJECT "${TRACYHCOM_SOURCE_FILE}")
	endif()

	set_target_properties(${_tracyhcom_obj_target} PROPERTIES
		POSITION_INDEPENDENT_CODE ON)

	# Host sources may include TracyHcom.hpp directly (e.g. USE_TRACY_HCOM_BUNDLE).
	target_include_directories(${TARGET} PRIVATE ${TRACYHCOM_INCLUDE_DIRS})

	target_include_directories(${_tracyhcom_obj_target} PRIVATE
		${TRACYHCOM_INCLUDE_DIRS}
		$<TARGET_PROPERTY:${TARGET},INCLUDE_DIRECTORIES>)
	target_compile_definitions(${_tracyhcom_obj_target} PRIVATE
		TRACY_ENABLE=1
		TRACY_DELAYED_INIT=1
		TRACY_SAVE_NO_SEND=1
		TRACY_ON_DEMAND=1
		TRACY_NO_BROADCAST=1
		TRACY_ONLY_LOCALHOST=1
		TRACY_NO_SAMPLING=1
		TRACY_NO_CONTEXT_SWITCH=1
	)

	# Perfetto feature gate
	if(HCOM_PERFETTO)
		target_compile_definitions(${_tracyhcom_obj_target} PRIVATE
			TRACYHCOM_ENABLE_PERFETTO=1
		)

		set(_tracyhcom_perfetto_wired FALSE)

		# Prefer an already-built perfetto_sdk target when available.
		if(TARGET perfetto_sdk)
			target_link_libraries(${TARGET} PRIVATE perfetto_sdk)
			target_include_directories(${_tracyhcom_obj_target} PRIVATE
				$<TARGET_PROPERTY:perfetto_sdk,INCLUDE_DIRECTORIES>)
			set(_tracyhcom_perfetto_wired TRUE)
		endif()

		# Fallback: compile perfetto amalgamated source directly into the object target.
		if(NOT _tracyhcom_perfetto_wired)
			if(DEFINED TRACYHCOM_PERFETTO_SDK_DIR)
				set(_tracyhcom_perfetto_sdk_dir "${TRACYHCOM_PERFETTO_SDK_DIR}")
			else()
				set(_tracyhcom_perfetto_sdk_dir "${TRACY_ROOT}/scripts/perfetto-sdk-offline")
			endif()

			if(EXISTS "${_tracyhcom_perfetto_sdk_dir}/perfetto.h")
				if(EXISTS "${_tracyhcom_perfetto_sdk_dir}/perfetto_protozero.cc")
					set(_tracyhcom_perfetto_src "${_tracyhcom_perfetto_sdk_dir}/perfetto_protozero.cc")
				elseif(EXISTS "${_tracyhcom_perfetto_sdk_dir}/perfetto.cc")
					set(_tracyhcom_perfetto_src "${_tracyhcom_perfetto_sdk_dir}/perfetto.cc")
				else()
					message(FATAL_ERROR
						"[TracyHcomBundle] PERFETTO requested but no implementation source found in: ${_tracyhcom_perfetto_sdk_dir}\n"
						"Expected one of: perfetto_protozero.cc, perfetto.cc")
				endif()

				target_sources(${_tracyhcom_obj_target} PRIVATE "${_tracyhcom_perfetto_src}")
				target_include_directories(${_tracyhcom_obj_target} PRIVATE
					"${_tracyhcom_perfetto_sdk_dir}")
				set(_tracyhcom_perfetto_wired TRUE)
			endif()
		endif()

		if(NOT _tracyhcom_perfetto_wired)
			message(FATAL_ERROR
				"[TracyHcomBundle] PERFETTO requested but no Perfetto SDK was wired.\n"
				"Provide one of:\n"
				"  1) target perfetto_sdk\n"
				"  2) TRACYHCOM_PERFETTO_SDK_DIR=/path/to/sdk (contains perfetto.h + perfetto.cc/protozero.cc)\n"
				"  3) ${TRACY_ROOT}/scripts/perfetto-sdk-offline with required files")
		endif()

		# TracyHcom.cpp #includes TracyLitePerfetto.cpp which is large;
		# MSVC needs /bigobj and the file must be compiled as C++17.
		# Also define NOMINMAX to prevent Windows min/max macros from breaking
		# generated perfetto.h symbols named min/max.
		if(MSVC)
			target_compile_options(${_tracyhcom_obj_target} PRIVATE /std:c++17 /bigobj)
			target_compile_definitions(${_tracyhcom_obj_target} PRIVATE NOMINMAX=1)
		else()
			target_compile_options(${_tracyhcom_obj_target} PRIVATE -std=c++17)
		endif()
	endif()

	# Host target only consumes compiled objects; no Tracy compile definitions are
	# attached to host sources.
	target_sources(${TARGET} PRIVATE $<TARGET_OBJECTS:${_tracyhcom_obj_target}>)
endfunction()

# ── INTERFACE target (alternative: use target_link_libraries for include/defs) 
# Creates tracyhcom_headers INTERFACE target that propagates include dirs only
# (sources still need to be added via tracyhcom_embed or manually).
if(NOT TARGET tracyhcom_headers)
	add_library(tracyhcom_headers INTERFACE)
	target_include_directories(tracyhcom_headers INTERFACE ${TRACYHCOM_INCLUDE_DIRS})
endif()
