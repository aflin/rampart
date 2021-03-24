cmake_minimum_required(VERSION 3.13)

project(TexisCore VERSION 8.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(INSTALL_SELF_CONTAINED ON CACHE BOOL "Install to a self contained directory")
#set(INSTALL_SELF_CONTAINED ON)
#set(INSTALL_DIRECTORY "/usr/local/texis" CACHE PATH "Installation Directory")
#set(CMAKE_INSTALL_PREFIX ${INSTALL_DIRECTORY})
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(WANT_SSL OFF CACHE BOOL "Include OpenSSL")
set(NO_RAM_EQUIVS OFF CACHE BOOL "Disable Builtin Thesaurus")
set(TEST_ATOMIC_FUNCS OFF CACHE BOOL "Test Thread Safe Atomic Functions")
set(HAVE_MMAP ON CACHE BOOL "Use mmap where possible")
set(LARGE_FILES ON CACHE BOOL "Support large files")
set(HAVE_RE2 ON CACHE BOOL "Include RE2 Support")
set(GENERATE_PIC OFF CACHE BOOL "Generate Position Independent Code (-fPIC)")
set(LOCK_SERVER ON CACHE BOOL "Use Lock Server")


add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)
if(LARGE_FILES)
  add_definitions(-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
endif(LARGE_FILES)
if(GENERATE_PIC)
  add_compile_options(-fPIC)
endif(GENERATE_PIC)
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
if(LOCK_SERVER)
include(thirdparty/libevent.cmake)
endif(LOCK_SERVER)
include(GNUInstallDirs)

include_directories(include)
include_directories(${CMAKE_BINARY_DIR})
if(HAVE_RE2)
include_directories(thirdparty/re2)
include_directories(thirdparty/cre2/src)

add_subdirectory(thirdparty/re2)
endif(HAVE_RE2)
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

#install(DIRECTORY include/ DESTINATION include/texis)
