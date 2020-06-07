set(CURL_PREFIX curl-7_70_0)

find_program(AUTORECONF NAMES autoreconf)
find_program(MAKE_EXE NAMES gmake nmake make)
ExternalProject_Add(${CURL_PREFIX}
	PREFIX ${CURL_PREFIX}
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG curl-7_70_0
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/${CURL_PREFIX}
  BUILD_COMMAND ${MAKE_EXE}
)
ExternalProject_Add_Step(${CURL_PREFIX} pre_configure 
  DEPENDEES download
  DEPENDERS configure
  COMMAND ${AUTORECONF} -vfi <SOURCE_DIR>/configure.ac
)

add_dependencies(${CURL_PREFIX} oniguruma)
add_dependencies(${CURL_PREFIX} openssl-1.1.1g)

ExternalProject_Get_Property(${CURL_PREFIX} SOURCE_DIR)
message(STATUS "Source directory of ${CURL_PREFIX} ${SOURCE_DIR}")

set(CURL_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${CURL_PREFIX}/lib)
set(CURL_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${CURL_PREFIX}/include)

include_directories(${CURL_INCLUDE_DIRS})
link_directories(${CURL_RELEASE_DIR})


