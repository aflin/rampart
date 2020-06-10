set(LIBEVENT_SOURCE_DIR ${EXTERN_DIR}/libevent)
set(LIBEVENT_TARGET_DIR ${CMAKE_BINARY_DIR}/libevent)
include(${EXTERN_DIR}/autoconf.cmake)

autoreconf(libevent ${LIBEVENT_SOURCE_DIR})

set(ENV{CPPFLAGS} -I${CMAKE_BINARY_DIR}/openssl/include)
set(ENV{LDFLAGS} -L${CMAKE_BINARY_DIR}/openssl/lib)
execute_process(
  COMMAND ./configure --prefix=${LIBEVENT_TARGET_DIR}
  WORKING_DIRECTORY ${LIBEVENT_SOURCE_DIR}
  RESULT_VARIABLE CONFIGURE_RESULT
)
if(NOT CONFIGURE_RESULT EQUAL "0")
  message(FATAL_ERROR "configure failed in ${LIBEVENT_SOURCE_DIR}")
endif()
execute_process(
  COMMAND make install
  WORKING_DIRECTORY ${LIBEVENT_SOURCE_DIR}
  RESULT_VARIABLE MAKE_RESULT
)
if(NOT MAKE_RESULT EQUAL "0")
  message(FATAL_ERROR "make failed in ${LIBEVENT_SOURCE_DIR}")
endif()