cmake_minimum_required(VERSION 3.13)

project(TexisCore VERSION 7.9.1 LANGUAGES C CXX)
add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)

include(thirdparty/jansson.cmake)


add_subdirectory(bootstrap)
add_subdirectory(mmapi)
add_subdirectory(texisapi)
add_subdirectory(apps)
add_subdirectory(docs)

include(CTest)
if(BUILD_TESTING)
add_subdirectory(test)
endif(BUILD_TESTING)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/include/texis)

set(HAVE_OPENSSL OFF BOOL "Whether to include alternate Texis SSL headers")