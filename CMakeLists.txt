cmake_minimum_required(VERSION 3.13)
project(isae_monitor VERSION 2.0.0 LANGUAGES C)

# C11 standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wformat=2 -Wunused)
    add_compile_options(-Wstrict-prototypes)
endif()

add_compile_definitions(
    _POSIX_C_SOURCE=200809L
    _GNU_SOURCE
    ISAE_VERSION_STRING="2.0.0"
)

# Required system packages (libcurl + libxml2).
# cJSON is vendored under third_party/cjson so the build does not depend on
# a libcjson -dev package being installed on the host.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)

# Project sources
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
    src/dotenv.c
)

# Vendored third-party sources
set(VENDOR_SOURCES
    third_party/cjson/cJSON.c
)

add_executable(isae_monitor ${SOURCES} ${VENDOR_SOURCES})

target_include_directories(isae_monitor PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/cjson
    ${CURL_INCLUDE_DIRS}
    ${LIBXML2_INCLUDE_DIRS}
)

target_link_libraries(isae_monitor PRIVATE
    ${CURL_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    m
)

target_link_directories(isae_monitor PRIVATE
    ${CURL_LIBRARY_DIRS}
    ${LIBXML2_LIBRARY_DIRS}
)

# Installation
include(GNUInstallDirs)
install(TARGETS isae_monitor
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Configuration summary
message(STATUS "----------------------------------------------------------")
message(STATUS "  isae_monitor ${PROJECT_VERSION} (C11)")
message(STATUS "----------------------------------------------------------")
message(STATUS "  libcurl   : ${CURL_VERSION} (include: ${CURL_INCLUDE_DIRS})")
message(STATUS "  libxml2   : ${LIBXML2_VERSION} (include: ${LIBXML2_INCLUDE_DIRS})")
message(STATUS "  cJSON     : vendored v1.7.18 (third_party/cjson)")
message(STATUS "  build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------------------------")
