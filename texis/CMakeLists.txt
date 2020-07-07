cmake_minimum_required(VERSION 3.13)

project(TexisCore VERSION 8.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(WANT_SSL OFF CACHE BOOL "Include OpenSSL")
set(NO_RAM_EQUIVS OFF CACHE BOOL "Disable Builtin Thesaurus")
set(TEST_ATOMIC_FUNCS OFF CACHE BOOL "Test Thread Safe Atomic Functions")
set(HAVE_MMAP ON CACHE BOOL "Use mmap where possible")
set(LARGE_FILES ON CACHE BOOL "Support large files")


add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)
if(LARGE_FILES)
  add_definitions(-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
endif(LARGE_FILES)
add_compile_options(-fPIC)
#
# include CPack to make install packages
#   will need conf
#
#set(CPACK_PACKAGE_VENDOR Thunderstone Software LLC)
#set(CPACK_PACKAGE_DESCRIPTION "Texis SQL text oriented Database core API")
#set(CPACK_GENERATOR "RPM")
#include(CPack)
include(CheckIncludeFile)
include(ExternalProject)
include(thirdparty/openssl.cmake)
include(thirdparty/jansson.cmake)
include(GNUInstallDirs)

include_directories(include)
include_directories(${CMAKE_BINARY_DIR})
include_directories(thirdparty/re2)
include_directories(thirdparty/cre2/src)

add_subdirectory(thirdparty/re2)
add_subdirectory(bootstrap)
add_subdirectory(mmapi)
add_subdirectory(texisapi)
add_subdirectory(apps)
add_subdirectory(docs)

# WTF - how to valgrind
#set(MEMORYCHECK_COMMAND_OPTIONS "--trace-children=yes --trace-children-skip=rm,diff,mv,awk" CACHE STRING "MEMORY CHECK OPTIONS")
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif(BUILD_TESTING)

install(DIRECTORY include/ DESTINATION include/texis)
