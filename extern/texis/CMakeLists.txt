cmake_minimum_required(VERSION 3.13)

project(TexisCore VERSION 8.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

#set(INSTALL_SELF_CONTAINED ON CACHE BOOL "Install to a self contained directory")
#set(INSTALL_SELF_CONTAINED ON)
#set(INSTALL_DIRECTORY "/usr/local/texis" CACHE PATH "Installation Directory")
#set(CMAKE_INSTALL_PREFIX ${INSTALL_DIRECTORY})

set(TEXIS_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
set(TEXIS_BINARY_DIR "${PROJECT_BINARY_DIR}")

set(CMAKE_MODULE_PATH "${TEXIS_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(WANT_SSL OFF CACHE BOOL "Include OpenSSL")
set(NO_RAM_EQUIVS OFF CACHE BOOL "Disable Builtin Thesaurus")
set(TEST_ATOMIC_FUNCS OFF CACHE BOOL "Test Thread Safe Atomic Functions")
set(HAVE_MMAP ON CACHE BOOL "Use mmap where possible")
set(LARGE_FILES ON CACHE BOOL "Support large files")
set(HAVE_RE2 ON CACHE BOOL "Include RE2 Support")
set(LOCK_SERVER ON CACHE BOOL "Use Lock Server")

set(LIBEVENT_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}/extern/libevent/include
    ${CMAKE_SOURCE_DIR}/extern/libevent/include
)

set(LIBEVENT_LIBRARY_DIRS "${CMAKE_BINARY_DIR}/extern/libevent/lib")

add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
add_compile_options(-fPIC -g)

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

# Silence warnings on vendored third-party code (Google's abseil/re2,
# jansson, libevent, openssl) when SILENCE_VENDORED_WARNINGS is ON.
# We don't maintain these, so warnings here are noise, not signal.
# Save/restore is scoped to the includes/add_subdirectory block below
# so -w doesn't leak into texisapi/texisbuild (which have their own
# precise audited list under TEXIS_SILENCE_AUDITED_WARNINGS).
if(SILENCE_VENDORED_WARNINGS)
    set(_TEXIS_TP_SAVED_C_FLAGS   "${CMAKE_C_FLAGS}")
    set(_TEXIS_TP_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -w")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
endif()

include(thirdparty/openssl.cmake)
include(thirdparty/jansson.cmake)
if(LOCK_SERVER)
include(thirdparty/libevent.cmake)
endif(LOCK_SERVER)
include(GNUInstallDirs)

include_directories(include)
include_directories(${TEXIS_BINARY_DIR})

#if(HAVE_RE2)
include_directories(thirdparty/re2)
include_directories(thirdparty/cre2/src)
include_directories(thirdparty/abseil-cpp)

# Some abseil stub libraries (e.g. absl_flags) contain no global symbols;
# Xcode's ranlib (and libtool) warn "table of contents is empty" for each,
# UNCONDITIONALLY — current (llvm-cctools) versions ignore
# -no_warning_for_no_symbols for the whole-archive case (verified on
# Xcode 16 / macOS 15.6).  llvm-ar already writes the symbol index during
# archive creation, so the ranlib finish step is redundant: skip it.
# Scoped to the vendored subdirs below; restored after re2.
if(APPLE)
    set(_TEXIS_TP_SAVED_C_ARCHIVE_FINISH   "${CMAKE_C_ARCHIVE_FINISH}")
    set(_TEXIS_TP_SAVED_CXX_ARCHIVE_FINISH "${CMAKE_CXX_ARCHIVE_FINISH}")
    set(CMAKE_C_ARCHIVE_FINISH   "")
    set(CMAKE_CXX_ARCHIVE_FINISH "")
endif()

add_subdirectory(thirdparty/abseil-cpp)

if(WIN32 OR CYGWIN)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(thirdparty/re2)

if(SILENCE_VENDORED_WARNINGS)
    set(CMAKE_C_FLAGS   "${_TEXIS_TP_SAVED_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${_TEXIS_TP_SAVED_CXX_FLAGS}")
endif()
if(APPLE)
    set(CMAKE_C_ARCHIVE_FINISH   "${_TEXIS_TP_SAVED_C_ARCHIVE_FINISH}")
    set(CMAKE_CXX_ARCHIVE_FINISH "${_TEXIS_TP_SAVED_CXX_ARCHIVE_FINISH}")
endif()

#endif(HAVE_RE2)

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
