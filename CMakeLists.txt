cmake_minimum_required(VERSION 3.21)

project(
	WeatherBehavior
	LANGUAGES CXX
	VERSION 1.0.0
	DESCRIPTION "Menu-driven weather-appropriate clothing for NPCs and followers"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static CRT globally to match vcpkg x64-windows-static triplet
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- CommonLibSSE-NG ----
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLib tests" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)

find_package(nlohmann_json CONFIG REQUIRED)

# ---- Plugin Sources ----
set(SOURCES
	src/Main.cpp
	src/Config.h
	src/Config.cpp
	src/Manager.h
	src/Manager.cpp
	src/Menu.h
	src/Menu.cpp
	src/Util.h
)

source_group(
	TREE "${CMAKE_CURRENT_SOURCE_DIR}"
	FILES ${SOURCES}
)

# ---- Build DLL ----
add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_compile_features(${PROJECT_NAME}
	PRIVATE
		cxx_std_23
)

target_include_directories(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/includes
)

target_compile_definitions(${PROJECT_NAME}
	PRIVATE
		UNICODE
		_UNICODE
		NOMINMAX
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		nlohmann_json::nlohmann_json
)

target_precompile_headers(${PROJECT_NAME}
	PRIVATE
		<RE/Skyrim.h>
		<SKSE/SKSE.h>
)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
	target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()

# ---- Output ----
if(DEFINED ENV{SKYRIM_MODS_FOLDER})
	set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/WeatherBehavior/SKSE/Plugins")
endif()

if(DEFINED OUTPUT_FOLDER)
	# Copy the output DLL to the configured output folder after build
	add_custom_command(
		TARGET ${PROJECT_NAME}
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_FOLDER}"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${OUTPUT_FOLDER}"
		VERBATIM
	)

	if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
		add_custom_command(
			TARGET ${PROJECT_NAME}
			POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${OUTPUT_FOLDER}"
			VERBATIM
		)
	endif()
endif()
