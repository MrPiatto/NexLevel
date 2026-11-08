cmake_minimum_required(VERSION 3.21)
# nexlevel_core: Geode-independent shared core (protocol, sync, session, transport).
# Built standalone for tests, or added via add_subdirectory() by each compat target.
project(nexlevel_core LANGUAGES CXX)

# Enforce the rule: no Geode or cocos2d includes anywhere in core/.
file(GLOB_RECURSE _NL_CORE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)
foreach(_f ${_NL_CORE_FILES})
    file(STRINGS ${_f} _bad REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](Geode/|cocos2d|cocos-ext)")
    if(_bad)
        message(FATAL_ERROR "nexlevel_core must stay Geode-free, but ${_f} includes: ${_bad}")
    endif()
endforeach()

# Standalone Asio (header-only, Boost Software License), pinned. Fetched at configure time
# into the build tree (inside NEXLEVEL). Used only from core .cpp files.
include(FetchContent)
FetchContent_Declare(nexlevel_asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-38-2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR nexlevel-headers-only)  # never add_subdirectory() Asio; headers only
FetchContent_MakeAvailable(nexlevel_asio)
# FetchContent strips the archive's top folder, so headers end up in <src>/include.
set(NEXLEVEL_ASIO_INCLUDE ${nexlevel_asio_SOURCE_DIR}/include)
if(NOT EXISTS ${NEXLEVEL_ASIO_INCLUDE}/asio.hpp)
    message(FATAL_ERROR "Asio headers not found at ${NEXLEVEL_ASIO_INCLUDE}/asio.hpp")
endif()
find_package(Threads REQUIRED)

add_library(nexlevel_core STATIC
    src/bytes.cpp
    src/frame.cpp
    src/net/loopback_probe.cpp
    src/net/local_address.cpp
    src/sync/object_props.cpp
    src/sync/op.cpp
    src/sync/replica.cpp
    src/sync/host_sequencer.cpp
    src/sync/client_replica.cpp
    src/session/messages.cpp
    src/session/host.cpp
    src/session/client.cpp
)
target_include_directories(nexlevel_core PRIVATE ${NEXLEVEL_ASIO_INCLUDE})
target_compile_definitions(nexlevel_core PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(nexlevel_core PUBLIC Threads::Threads)
if(WIN32)
    target_compile_definitions(nexlevel_core PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(nexlevel_core PUBLIC ws2_32 mswsock)
endif()
target_include_directories(nexlevel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nexlevel_core PUBLIC cxx_std_20)
set_target_properties(nexlevel_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden)

option(NEXLEVEL_BUILD_TESTS "Build nexlevel_core unit tests" ${PROJECT_IS_TOP_LEVEL})
if(NEXLEVEL_BUILD_TESTS)
    enable_testing()
    add_executable(nexlevel_core_tests tests/test_main.cpp tests/test_sync.cpp tests/test_session.cpp)
    target_link_libraries(nexlevel_core_tests PRIVATE nexlevel_core)
    add_test(NAME nexlevel_core_tests COMMAND nexlevel_core_tests)
endif()
