cmake_minimum_required(VERSION 3.6.0)
project(bang-launcher VERSION 0.1.0)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "")
set(ENABLE_GNUTLS OFF CACHE BOOL "")
set(ENABLE_MBEDTLS OFF CACHE BOOL "")
set(ENABLE_OPENSSL OFF CACHE BOOL "")
set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "")
set(ENABLE_BZIP2 OFF CACHE BOOL "")
set(ENABLE_LZMA OFF CACHE BOOL "")
set(ENABLE_ZSTD OFF CACHE BOOL "")
set(BUILD_TOOLS OFF CACHE BOOL "")
set(BUILD_REGRESS OFF CACHE BOOL "")
set(BUILD_EXAMPLES OFF CACHE BOOL "")
set(BUILD_DOC OFF CACHE BOOL "")

add_subdirectory(external/libzip)

add_library(cjson_static OBJECT external/cjson/cJSON.c)
target_include_directories(cjson_static PUBLIC external/)

if(NOT WIN32)
    message(SEND_ERROR "Launcher is built on Windows API")
endif()

add_executable(banglauncher WIN32 main.c bang.rc)
target_link_libraries(banglauncher libzip::zip cjson_static shlwapi wininet comctl32)