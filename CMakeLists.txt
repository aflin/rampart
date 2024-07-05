## to build a macos universal, run:
#   cmake -DMACOS_UNIVERSAL=1 ../

## to build with -fsanitize=address
#   cmake -DRP_SANITIZE=1 ../
# and maybe export ASAN_OPTIONS=fast_unwind_on_malloc=false
# if stack traces are incomplete.
# and if python export PYTHONMALLOC=malloc
# or pymalloc will be used and asan will miss massive amounts of frees

cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.2.3 LANGUAGES C CXX)

#set(CMAKE_BUILD_TYPE Debug)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")


if (MACOS_UNIVERSAL)

	message("Making MacOs Universal Binary Build")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -mmacosx-version-min=10.13 -arch arm64")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch x86_64 -mmacosx-version-min=10.13 -arch arm64")

else(MACOS_UNIVERSAL)

if( CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" )
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=11.0 -arch arm64")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=11.0 -arch arm64")
endif( CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" )

endif (MACOS_UNIVERSAL)

endif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

message("###### CONFIGURING DEPENDENCIES ######")

include(extern/extern.cmake)

message("######   CONFIGURING RAMPART    ######")

add_subdirectory(src)

include(cmake/ExternalProject.cmake)

set(LIBEVENT_INCLUDE_DIRS
    ${CMAKE_CURRENT_BINARY_DIR}/extern/libevent/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/libevent/include
)

string(REPLACE ";" "|" LIBEVENT_INCLUDE_ALT "${LIBEVENT_INCLUDE_DIRS}")

ExternalProject_Add( texis
	PREFIX texis
	SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/texis"
	LIST_SEPARATOR |
	CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Debug
		-DGENERATE_PIC=1
		-DLIBEVENT_INCLUDE_DIRS=${LIBEVENT_INCLUDE_ALT}
		-DLIBEVENT_LIBRARY_DIRS=${CMAKE_CURRENT_BINARY_DIR}/extern/libevent/lib
		-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
		-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
	BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/texis" 
	INSTALL_COMMAND cmake -E echo "Skipping install step."
	DEPENDS openssl event_static event_core_static event_pthreads_static
)
