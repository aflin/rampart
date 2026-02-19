## to build a macos universal, run:
#   cmake -DMACOS_UNIVERSAL=1 ../

## to build with -fsanitize=address
#   cmake -DRP_SANITIZE=1 ../
# and maybe export ASAN_OPTIONS=fast_unwind_on_malloc=false
# if stack traces are incomplete.
# and if python export PYTHONMALLOC=malloc
# or pymalloc will be used and asan will miss massive amounts of frees

cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.5.0 LANGUAGES C CXX)

# Set default install prefix (before any add_subdirectory calls)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(CYGWIN)
        set(CMAKE_INSTALL_PREFIX "/c/Program Files/mcs/rampart" CACHE PATH "" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/usr/local/rampart" CACHE PATH "" FORCE)
    endif()
    message("setting install = ${CMAKE_INSTALL_PREFIX}")
else()
    message("install = ${CMAKE_INSTALL_PREFIX}")
endif()

if(CYGWIN)
  # GCC 15 defaults to C23 which breaks many third-party libraries and cmake
  # configure checks (K&R function declarations, 0 as null pointer, etc.).
  # Force gnu11 for compatibility. Using CMAKE_C_FLAGS ensures this applies
  # to both cmake try_compile checks and actual compilation.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
endif()

#set(CMAKE_BUILD_TYPE Debug)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

    # -Wno-deprecated-non-prototype added in AppleClang 13+
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "13.0")
        add_compile_options(-Wno-deprecated-non-prototype)
    endif()

    #no longer supported - libraries used and python aren't universal
    if (MACOS_UNIVERSAL)

        message("Making MacOs Universal Binary Build")
        SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -mmacosx-version-min=11.0 -arch arm64")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch x86_64 -mmacosx-version-min=11.0 -arch arm64")

    else(MACOS_UNIVERSAL)

        if( CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" )
            SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=11.0 -arch arm64")
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=11.0 -arch arm64")
	else()
            SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -mmacosx-version-min=11.0")
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch x86_64 -mmacosx-version-min=11.0")
        endif( CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" )

    endif (MACOS_UNIVERSAL)

endif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

message("###### CONFIGURING DEPENDENCIES ######")

include(extern/extern.cmake)

message("######   CONFIGURING RAMPART    ######")

add_subdirectory(src)

include(cmake/ExternalProject.cmake)

