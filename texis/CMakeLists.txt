cmake_minimum_required(VERSION 3.13)

project(TexisCore VERSION 7.9.1 LANGUAGES C CXX)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(WANT_SSL OFF CACHE BOOL "Include OpenSSL")
set(NO_RAM_EQUIVS OFF CACHE BOOL "Disable Builtin Thesaurus")
set(TEST_ATOMIC_FUNCS OFF CACHE BOOL "Test Thread Safe Atomic Functions")
#set(CMAKE_BUILD_TYPE Debug)

add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)

include(CheckIncludeFile)
include(ExternalProject)
include(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/jansson.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR})
add_subdirectory(bootstrap)
add_subdirectory(mmapi)
add_subdirectory(texisapi)
add_subdirectory(apps)
add_subdirectory(docs)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif(BUILD_TESTING)

# install(DIRECTORY include/ DESTINATION include/texis)