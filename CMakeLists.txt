cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.0.1 LANGUAGES C CXX)

#set(CMAKE_BUILD_TYPE Debug)

#texis must be cmaked and compiled separately from all this or errors will
#ensue, especially for Macos

if (
	(NOT EXISTS "${CMAKE_BINARY_DIR}/texis/texisapi/libtexisapi.a")
	OR
	( 
	  "${CMAKE_SOURCE_DIR}/texis/CMakeLists.txt" 
		IS_NEWER_THAN
	  "${CMAKE_BINARY_DIR}/texis/CMakeFiles/cmake.check_cache"
	)
)

execute_process(
	COMMAND "mkdir" "-p" "${CMAKE_BINARY_DIR}/texis"
	COMMAND_ECHO STDOUT
	RESULT_VARIABLE ret
)
if(NOT ret EQUAL 0)
	message( FATAL_ERROR "Texis build failed")
endif()

message("######      BUILDING TEXIS      ######")

execute_process(
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/texis"
	COMMAND "cmake" "../../texis"
	COMMAND_ECHO STDOUT
	RESULT_VARIABLE ret
)
if(NOT ret EQUAL 0)
	message( FATAL_ERROR "Texis build failed")
endif()

execute_process(
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/texis"
	COMMAND "make"
	COMMAND_ECHO STDOUT
	RESULT_VARIABLE ret
)

if(NOT ret EQUAL 0)
	message( FATAL_ERROR "Texis build failed")
endif()

execute_process(
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/texis"
	COMMAND "make" "test"
	COMMAND_ECHO STDOUT
	RESULT_VARIABLE ret
)

if(NOT ret EQUAL 0)
	message( FATAL_ERROR "Texis build failed")
endif()

endif()

message("###### CONFIGURING DEPENDENCIES ######")

include(extern/extern.cmake)
#include_directories(${CMAKE_BINARY_DIR})
#include_directories(${CMAKE_SOURCE_DIR}/extern/libevent/include)
#include_directories(${CMAKE_SOURCE_DIR}/extern/libevhtp/include)
#include_directories(${CMAKE_BINARY_DIR}/texis/texisapi/)

message("######   CONFIGURING RAMPART    ######")

add_subdirectory(src)
add_subdirectory(examples)
enable_testing()
add_subdirectory(test)

