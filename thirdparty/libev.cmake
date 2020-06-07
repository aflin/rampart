set(LIBEV_PREFIX libev)
find_program(MAKE_EXE NAMES gmake nmake make)

ExternalProject_Add(${LIBEV_PREFIX}
PREFIX ${LIBEV_PREFIX}
GIT_REPOSITORY https://github.com/libevent/libevent.git
GIT_TAG release-2.1.11-stable
CONFIGURE_COMMAND CPPFLAGS=-I${OPENSSL_INCLUDE_DIRS} LDFLAGS=-L${OPENSSL_RELEASE_DIR} <SOURCE_DIR>/configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/${LIBEV_PREFIX} 
BUILD_COMMAND ${MAKE_EXE}
)

ExternalProject_Add_Step(${LIBEV_PREFIX} pre_configure 
DEPENDEES download
DEPENDERS configure
COMMAND ${AUTORECONF} -vfi <SOURCE_DIR>/configure.ac
)


add_dependencies(libev openssl-1.1.1g)
ExternalProject_Get_Property(${LIBEV_PREFIX} SOURCE_DIR)

message(STATUS "Source directory of ${LIBEV_PREFIX} ${SOURCE_DIR}")

set(LIBEV_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${LIBEV_PREFIX}/lib)
set(LIBEV_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${LIBEV_PREFIX}/include)