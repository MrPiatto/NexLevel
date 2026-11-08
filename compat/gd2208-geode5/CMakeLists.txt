cmake_minimum_required(VERSION 3.21)
# NexLevel build for GD 2.2081 (Geode v5). One CMake build = one Geode SDK, so each target is its own project.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(NexLevel VERSION 0.1.0)

set(NEXLEVEL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    ${NEXLEVEL_ROOT}/shared-geode/src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NEXLEVEL_TARGET=2
    NEXLEVEL_GEODE_MAJOR=5)

# Shared Geode-free core.
set(NEXLEVEL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${NEXLEVEL_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/nexlevel_core)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Use tools/build.ps1, or set GEODE_SDK to deps/geode-sdk/<tag> for this target")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})

# Plain signature: setup_geode_mod links geode-sdk with an empty LINK_TYPE by default (verified in SDK cmake/GeodeFile.cmake for v4.10.2, v5.10.1, v6.0.0-alpha.1).
target_link_libraries(${PROJECT_NAME} nexlevel_core)
