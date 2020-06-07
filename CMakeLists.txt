cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(NO_RAM_EQUIVS OFF CACHE BOOL "Disable Builtin Thesaurus")
set(TEST_ATOMIC_FUNCS OFF CACHE BOOL "Test Thread Safe Atomic Functions")
set(CMAKE_BUILD_TYPE Debug)

add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)

include(CheckIncludeFile)
include(ExternalProject)
include(thirdparty/gawk.cmake)
include(thirdparty/curl.cmake)
include(thirdparty/oniguruma.cmake)
include(thirdparty/openssl.cmake)
include(thirdparty/libev.cmake)
include(thirdparty/libevhtp.cmake)
include(thirdparty/jansson.cmake)
include_directories(${CMAKE_BINARY_DIR})
include_directories(include)

add_subdirectory(texis)
add_subdirectory(src)
