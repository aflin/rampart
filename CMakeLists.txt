cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.0.1 LANGUAGES C CXX)

#set(CMAKE_BUILD_TYPE Debug)

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
		-DGENERATE_PIC=1
		-DLIBEVENT_INCLUDE_DIRS=${LIBEVENT_INCLUDE_ALT}
		-DLIBEVENT_LIBRARY_DIRS=${CMAKE_CURRENT_BINARY_DIR}/extern/libevent/lib
	BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/texis" 
	INSTALL_COMMAND cmake -E echo "Skipping install step."
	DEPENDS openssl event_static event_core_static event_pthreads_static
)
