cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.1.3 LANGUAGES C CXX)

#set(CMAKE_BUILD_TYPE Debug)

# timespec_get
set(CMAKE_C_STANDARD 11)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

## to build a macos universal, run:
#   cmake -DMACOS_UNIVERSAL=1 ../

if (MACOS_UNIVERSAL)
	message("Making MacOs Universal Binary Build")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -mmacosx-version-min=10.13 -arch arm64")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch x86_64 -mmacosx-version-min=10.13 -arch arm64")
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
