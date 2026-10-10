cmake_minimum_required(VERSION 3.28)

project(webclap-plugins VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if (NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "wasm32")
	message(FATAL_ERROR "Configure with the WASI-SDK toolchain, e.g. `cmake --preset wasm`")
endif()

include(FetchContent)
FetchContent_Declare(
	clap
	GIT_REPOSITORY https://github.com/free-audio/clap.git
	GIT_TAG 1.2.7
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(clap)

include(cmake/wclap.cmake)

# The build label shown in the plugin UIs (commit date + ID), refreshed on every build
set(buildInfoDir ${CMAKE_BINARY_DIR}/generated/build-info)
add_custom_target(webclap-build-info ALL
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR} -DOUTPUT=${buildInfoDir}/webclap/build-info.h -P ${CMAKE_CURRENT_LIST_DIR}/cmake/build-info.cmake
	BYPRODUCTS ${buildInfoDir}/webclap/build-info.h
)

add_library(webclap-shared INTERFACE)
target_include_directories(webclap-shared INTERFACE ${CMAKE_CURRENT_LIST_DIR}/shared ${buildInfoDir})
target_link_libraries(webclap-shared INTERFACE clap)
add_dependencies(webclap-shared webclap-build-info)

# ---- Milestone 0: Airwindows port ----

add_wclap_bundle(
	NAME airwindows
	SOURCES
		${CMAKE_CURRENT_LIST_DIR}/plugins/airwindows/entry.cpp
	RESOURCES_DIR ${CMAKE_CURRENT_LIST_DIR}/plugins/airwindows/ui
	EXTRA_FILES ${CMAKE_CURRENT_LIST_DIR}/LICENSE.txt
)

# ---- Console EQ ----

add_wclap_bundle(
	NAME console-eq
	SOURCES
		${CMAKE_CURRENT_LIST_DIR}/plugins/console-eq/entry.cpp
	RESOURCES_DIR ${CMAKE_CURRENT_LIST_DIR}/plugins/console-eq/ui
	EXTRA_FILES ${CMAKE_CURRENT_LIST_DIR}/LICENSE.txt
)
