cmake_minimum_required(VERSION 3.21)
# nexlevel_core: Geode-independent shared core (protocol, sync, session, transport).
# Built standalone for tests, or added via add_subdirectory() by each compat target.
project(nexlevel_core LANGUAGES C CXX)

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

# Monocypher 4.0.2 (CC0 / BSD-2), pinned: X25519, BLAKE2b, XChaCha20-Poly1305 for session encryption.
# Two files, compiled into a small private static library.
FetchContent_Declare(nexlevel_monocypher
    URL https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR nexlevel-no-cmake)
FetchContent_MakeAvailable(nexlevel_monocypher)
set(NEXLEVEL_MONOCYPHER_SRC ${nexlevel_monocypher_SOURCE_DIR}/src)
if(NOT EXISTS ${NEXLEVEL_MONOCYPHER_SRC}/monocypher.c)
    message(FATAL_ERROR "Monocypher not found at ${NEXLEVEL_MONOCYPHER_SRC}/monocypher.c")
endif()
add_library(nexlevel_monocypher STATIC ${NEXLEVEL_MONOCYPHER_SRC}/monocypher.c)
target_include_directories(nexlevel_monocypher PUBLIC ${NEXLEVEL_MONOCYPHER_SRC})
set_target_properties(nexlevel_monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)

find_package(Threads REQUIRED)

add_library(nexlevel_core STATIC
    src/bytes.cpp
    src/frame.cpp
    src/net/loopback_probe.cpp
    src/net/local_address.cpp
    src/net/upnp.cpp
    src/sync/object_props.cpp
    src/sync/op.cpp
    src/sync/replica.cpp
    src/sync/host_sequencer.cpp
    src/sync/client_replica.cpp
    src/sync/level_settings.cpp
    src/session/messages.cpp
    src/session/room_code.cpp
    src/session/host.cpp
    src/session/client.cpp
    src/crypto/secure_channel.cpp
    src/relay/protocol.cpp
    src/relay/relay_server.cpp
)
target_include_directories(nexlevel_core PRIVATE ${NEXLEVEL_ASIO_INCLUDE})
target_compile_definitions(nexlevel_core PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(nexlevel_core PUBLIC Threads::Threads PRIVATE nexlevel_monocypher)
if(WIN32)
    target_compile_definitions(nexlevel_core PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(nexlevel_core PUBLIC ws2_32 mswsock bcrypt)
endif()
target_include_directories(nexlevel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nexlevel_core PUBLIC cxx_std_20)
set_target_properties(nexlevel_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden)

option(NEXLEVEL_BUILD_TESTS "Build nexlevel_core unit tests" ${PROJECT_IS_TOP_LEVEL})
if(NEXLEVEL_BUILD_TESTS)
    enable_testing()
    add_executable(nexlevel_core_tests tests/test_main.cpp tests/test_sync.cpp tests/test_session.cpp tests/test_upnp.cpp tests/test_crypto.cpp tests/test_relay.cpp tests/test_level_settings.cpp)
    target_link_libraries(nexlevel_core_tests PRIVATE nexlevel_core)
    # Network tests drive raw sockets / mock servers with Asio directly.
    target_include_directories(nexlevel_core_tests PRIVATE ${NEXLEVEL_ASIO_INCLUDE})
    target_compile_definitions(nexlevel_core_tests PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    if(WIN32)
        target_compile_definitions(nexlevel_core_tests PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    add_test(NAME nexlevel_core_tests COMMAND nexlevel_core_tests)
endif()

option(NEXLEVEL_BUILD_TOOLS "Build developer tools (nexlevel_bot)" ${PROJECT_IS_TOP_LEVEL})
if(NEXLEVEL_BUILD_TOOLS)
    add_executable(nexlevel_bot tools/nexlevel_bot.cpp)
    target_link_libraries(nexlevel_bot PRIVATE nexlevel_core)
    add_executable(nexlevel_relay tools/nexlevel_relay.cpp)
    target_link_libraries(nexlevel_relay PRIVATE nexlevel_core)
endif()
