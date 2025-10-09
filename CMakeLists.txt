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

#set(CMAKE_BUILD_TYPE Debug)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
add_compile_options(-Wno-deprecated-non-prototype)

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

