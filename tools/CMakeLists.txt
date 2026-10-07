cmake_minimum_required(VERSION 3.16)
project(ble_heartbeat_test C)

# Path to zenoh-pico source tree (built with Z_FEATURE_LINK_SERIAL=1)
set(ZENOH_PICO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../zenoh-pico" CACHE PATH "Path to zenoh-pico source")
set(ZENOH_PICO_BUILD_DIR "${ZENOH_PICO_DIR}/build" CACHE PATH "Path to zenoh-pico build directory")

# Verify zenoh-pico exists
if(NOT EXISTS "${ZENOH_PICO_DIR}/include/zenoh-pico.h")
    message(FATAL_ERROR "zenoh-pico not found at ${ZENOH_PICO_DIR}. "
                        "Set ZENOH_PICO_DIR to the zenoh-pico source directory.")
endif()

# Detect platform define (zenoh-pico requires this)
if(APPLE)
    set(ZENOH_PLATFORM_DEFINE ZENOH_MACOS)
elseif(UNIX)
    set(ZENOH_PLATFORM_DEFINE ZENOH_LINUX)
elseif(WIN32)
    set(ZENOH_PLATFORM_DEFINE ZENOH_WINDOWS)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Heartbeat publisher
add_executable(heartbeat_pub heartbeat_pub.c)

# Platform define must match what zenoh-pico was built with
target_compile_definitions(heartbeat_pub PRIVATE ${ZENOH_PLATFORM_DEFINE})

# Include directories: source includes (config.h is generated in-place by cmake)
target_include_directories(heartbeat_pub PRIVATE
    ${ZENOH_PICO_DIR}/include
)

# Link against the built zenoh-pico shared library
find_library(ZENOHPICO_LIB zenohpico PATHS ${ZENOH_PICO_BUILD_DIR}/lib NO_DEFAULT_PATH)
if(NOT ZENOHPICO_LIB)
    message(FATAL_ERROR "libzenohpico not found in ${ZENOH_PICO_BUILD_DIR}/lib. "
                        "Build zenoh-pico first:\n"
                        "  cmake -S ${ZENOH_PICO_DIR} -B ${ZENOH_PICO_BUILD_DIR} -DZ_FEATURE_LINK_SERIAL=1\n"
                        "  cmake --build ${ZENOH_PICO_BUILD_DIR}")
endif()

target_link_libraries(heartbeat_pub PRIVATE ${ZENOHPICO_LIB})

# macOS needs pthread
find_package(Threads REQUIRED)
target_link_libraries(heartbeat_pub PRIVATE Threads::Threads)

message(STATUS "zenoh-pico source: ${ZENOH_PICO_DIR}")
message(STATUS "zenoh-pico build:  ${ZENOH_PICO_BUILD_DIR}")
message(STATUS "zenoh-pico lib:    ${ZENOHPICO_LIB}")
message(STATUS "Platform:          ${ZENOH_PLATFORM_DEFINE}")
