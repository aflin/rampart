cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(WANT_SSL OFF CACHE BOOL "Include OpenSSL")
set(NO_RAM_EQUIVS OFF CACHE BOOL "Disable Builtin Thesaurus")
set(TEST_ATOMIC_FUNCS OFF CACHE BOOL "Test Thread Safe Atomic Functions")
set(CMAKE_BUILD_TYPE Debug)

add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)

include(CheckIncludeFile)
include(ExternalProject)
include(thirdparty/openssl.cmake)
include(thirdparty/jansson.cmake)
include_directories(${CMAKE_BINARY_DIR})
include_directories(include)

add_subdirectory(texis)
add_subdirectory(src)
