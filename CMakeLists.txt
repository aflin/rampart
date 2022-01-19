cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.0.1 LANGUAGES C CXX)

#set(CMAKE_BUILD_TYPE Debug)

message("###### CONFIGURING DEPENDENCIES ######")

include(extern/extern.cmake)

message("######   CONFIGURING RAMPART    ######")

add_subdirectory(src)

include(cmake/ExternalProject.cmake)
ExternalProject_Add( texis
	PREFIX texis
	SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/texis"
	CMAKE_ARGS "-DGENERATE_PIC=1"
	BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/texis" 
	INSTALL_COMMAND cmake -E echo "Skipping install step."
	DEPENDS openssl event_static event_core_static event_pthreads_static
)
