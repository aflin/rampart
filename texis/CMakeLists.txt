cmake_minimum_required(VERSION 3.13)

project(TexisCore VERSION 7.9.1 LANGUAGES C CXX)
add_definitions(-D_XOPEN_SOURCE -D_GNU_SOURCE)
add_subdirectory(bootstrap)
add_subdirectory(mmapi)
add_subdirectory(texisapi)
add_subdirectory(apps)
add_subdirectory(docs)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif(BUILD_TESTING)

install(DIRECTORY include DESTINATION include/texis)

set(HAVE_OPENSSL OFF BOOL "Whether to include alternate Texis SSL headers")