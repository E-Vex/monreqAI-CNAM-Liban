cmake_minimum_required(VERSION 3.10)
project(isae_monitor VERSION 1.0.0 LANGUAGES C)

# C11 standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wformat=2 -Wunused)
    add_compile_options(-Wstrict-prototypes -Wmissing-prototypes)
endif()

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules(CJSON REQUIRED libcjson)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${LIBXML2_INCLUDE_DIRS})
include_directories(${CJSON_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/main.c
    src/departments.c
    src/models.c
    src/config.c
    src/state.c
    src/httpclient.c
    src/feed.c
    src/providers.c
    src/keywords.c
    src/classifier.c
    src/telegram.c
    src/pipeline.c
)

# Create executable
add_executable(isae_monitor ${SOURCES})

# Link libraries
target_link_libraries(isae_monitor
    ${CURL_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    ${CJSON_LIBRARIES}
    m  # Math library
)

# Library link directories
target_link_directories(isae_monitor PRIVATE
    ${CURL_LIBRARY_DIRS}
    ${LIBXML2_LIBRARY_DIRS}
    ${CJSON_LIBRARY_DIRS}
)

# Installation
install(TARGETS isae_monitor DESTINATION bin)

# Print configuration
message(STATUS "CURL include dirs: ${CURL_INCLUDE_DIRS}")
message(STATUS "LibXML2 include dirs: ${LIBXML2_INCLUDE_DIRS}")
message(STATUS "CJSON include dirs: ${CJSON_INCLUDE_DIRS}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
