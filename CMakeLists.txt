cmake_minimum_required(VERSION 3.20)

macro(set_from_environment VARIABLE)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif ()
endmacro()

include(cmake/version.cmake)

project(
	EngineFixes
	VERSION ${VERSION}
	LANGUAGES CXX
)

option(BUILD_SKYRIMVR "Build for Skyrim VR" OFF)
option(BUILD_SKYRIMAE "Build for Skyrim AE" OFF)

macro(set_from_environment VARIABLE)
	if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif()
endmacro()

macro(find_commonlib_path)
	if(CommonLibName AND NOT ${CommonLibName} STREQUAL "")
		# Check extern
		find_path(CommonLibPath
			include/REL/Relocation.h
			PATHS external/${CommonLibName}
			extern/${CommonLibName})

		if(${CommonLibPath} STREQUAL "CommonLibPath-NOTFOUND")
			# Check path
			set_from_environment(${CommonLibName}Path)
			set(CommonLibPath ${${CommonLibName}Path})
		endif()
	endif()
endmacro()

if(BUILD_SKYRIMAE)
	add_compile_definitions(SKYRIM_AE)
	add_compile_definitions(SKYRIM_SUPPORT_AE)
	set(CommonLibName "CommonLibSSE")
	set_from_environment(SkyrimAEPath)
	set(SkyrimPath ${SkyrimAEPath})
	set(SkyrimVersion "Skyrim AE")
	set(VERSION ${VERSION}.${AE_VERSION})
elseif(BUILD_SKYRIMVR)
	add_compile_definitions(SKYRIMVR)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
	set(CommonLibName "CommonLibVR")
	set_from_environment(SkyrimVRPath)
	set(SkyrimPath ${SkyrimVRPath})
	set(SkyrimVersion "Skyrim VR")
	set(VERSION ${VERSION}.${VR_VERSION})
else()
	set(CommonLibName "CommonLibSSE")
	set_from_environment(Skyrim64Path)
	set(SkyrimPath ${Skyrim64Path})
	set(SkyrimVersion "Skyrim SSE")
endif()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/Version.h"
	@ONLY
)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.rc"
	@ONLY
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

add_compile_definitions(
	SKSE_SUPPORT_XBYAK
    _UNICODE
    UNICODE
)


find_commonlib_path()
message(
	STATUS
	"Building ${NAME} ${VERSION} for ${SkyrimVersion} at ${SkyrimPath} with ${CommonLibName} at ${CommonLibPath}."
)

find_package(AutoTOML REQUIRED CONFIG)
find_package(Boost
	MODULE
	REQUIRED
	COMPONENTS
		regex
)
find_package(TBB
	REQUIRED
	CONFIG
	COMPONENTS
		tbb
		tbbmalloc
	CONFIG
)
find_package(xbyak REQUIRED CONFIG)

if(DEFINED CommonLibPath AND NOT ${CommonLibPath} STREQUAL "" AND IS_DIRECTORY ${CommonLibPath})
	add_subdirectory(${CommonLibPath} ${CommonLibName})
else()
	message(
		FATAL_ERROR
		"Variable ${CommonLibName}Path is not set or in extern/."
	)
endif()

include(cmake/sourcelist.cmake)

source_group(
	TREE ${CMAKE_CURRENT_SOURCE_DIR}
	FILES ${SOURCES}
)

source_group(
	TREE ${CMAKE_CURRENT_BINARY_DIR}
	FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

add_library(
	${PROJECT_NAME}
	SHARED
	${SOURCES}
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	${PROJECT_NAME}.toml
	.clang-format
	.editorconfig
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
		cxx_std_20
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		AutoTOML::AutoTOML
		Boost::regex
		${CommonLibName}::${CommonLibName}
		TBB::tbb
		TBB::tbbmalloc
		xbyak::xbyak
)

if (MSVC)
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		src/PCH.h
)

option(COPY_BUILD "whether we should copy the outputs to the skyrim dir" OFF)
if(COPY_BUILD)
    if (DEFINED SkyrimPath)
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" ${SkyrimPath}/Data/SKSE/Plugins/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" ${SkyrimPath}/Data/SKSE/Plugins/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}_preload.txt" ${SkyrimPath}/Data/SKSE/Plugins/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}_SNCT.toml" ${SkyrimPath}/Data/SKSE/Plugins/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.toml" ${SkyrimPath}/Data/SKSE/Plugins/
        )
    else()
        message(WARNING "SkyrimPath is not defined: skipping post-build copy")
    endif()
endif()

set(Python3_FIND_STRATEGY "VERSION")
find_package(Python3
	MODULE
	REQUIRED
	COMPONENTS
		Interpreter
)

if(TARGET Python3::Interpreter)
	set(SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/archive_artifacts.py)
	set(TARGET_NAME "archive")

	add_custom_target(
		${TARGET_NAME}
		COMMAND
			"$<TARGET_FILE:Python3::Interpreter>"
			"${SCRIPT}"
			"--dll=$<TARGET_FILE:${PROJECT_NAME}>"
			"--name=${PROJECT_NAME}"
			"--out-dir=${TARGET_NAME}"
			"--pdb=$<TARGET_PDB_FILE:${PROJECT_NAME}>"
			"--src-dir=${CMAKE_CURRENT_SOURCE_DIR}"
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		VERBATIM
		SOURCES
			${SCRIPT}
	)

	add_dependencies(${TARGET_NAME} ${PROJECT_NAME})
else()
	message(WARNING "failed to find python interpreter: skipping archive target")
endif()