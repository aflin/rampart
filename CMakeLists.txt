cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.0.1 LANGUAGES C CXX)

include(extern/extern.cmake)
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_SOURCE_DIR}/texis/include)
add_subdirectory(texis)
add_subdirectory(src)
