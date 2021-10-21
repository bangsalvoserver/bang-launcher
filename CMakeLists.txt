cmake_minimum_required(VERSION 3.6.0)
project(bang-launcher VERSION 0.1.0)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

set(BUILD_CURL_EXE OFF CACHE BOOL "")
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "")
set(CMAKE_USE_LIBSSH2 OFF CACHE BOOL "")
set(CMAKE_USE_OPENSSL ON CACHE BOOL "")

add_subdirectory(external/libcurl)

set(BUILD_TOOLS OFF CACHE BOOL "")
set(BUILD_REGRESS OFF CACHE BOOL "")
set(BUILD_EXAMPLES OFF CACHE BOOL "")
set(BUILD_DOC OFF CACHE BOOL "")
set(ENABLE_BZIP2 OFF CACHE BOOL "")
set(ENABLE_LZMA OFF CACHE BOOL "")
set(ENABLE_ZSTD OFF CACHE BOOL "")

add_subdirectory(external/libzip)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "")
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "")
set(ENABLE_LOCALES OFF CACHE BOOL "")

add_library(cjson_static OBJECT external/cjson/cJSON.c)
target_include_directories(cjson_static PUBLIC external/)

set(launcher_srcs main.c)
if(WIN32)
    LIST(APPEND launcher_srcs bang.rc)
endif()

add_executable(banglauncher ${launcher_srcs})
target_link_libraries(banglauncher libcurl libzip::zip cjson_static)
if(WIN32)
    target_link_libraries(banglauncher shlwapi)
endif()