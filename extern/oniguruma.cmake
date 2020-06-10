set(ONIGURUMA_SOURCE_DIR ${EXTERN_DIR}/oniguruma)
set(ONIGURUMA_TARGET_DIR ${CMAKE_BINARY_DIR}/oniguruma)
include(${EXTERN_DIR}/autoconf.cmake)


autoreconf(oniguruma ${ONIGURUMA_SOURCE_DIR})

set(ENV{CFLAGS} -fPIC)
execute_process(
  COMMAND ./configure --enable-posix-api --prefix=${ONIGURUMA_TARGET_DIR}
  WORKING_DIRECTORY ${ONIGURUMA_SOURCE_DIR}
  RESULT_VARIABLE CONFIGURE_RESULT
)
if(NOT CONFIGURE_RESULT EQUAL "0")
  message(FATAL_ERROR "configure failed in ${ONIGURUMA_SOURCE_DIR}")
endif()

execute_process( 
  COMMAND make install
  WORKING_DIRECTORY ${ONIGURUMA_SOURCE_DIR}
  RESULT_VARIABLE MAKE_RESULT
)
if(NOT MAKE_RESULT EQUAL "0")
  message(FATAL_ERROR "make failed in ${ONIGURUMA_SOURCE_DIR}")
endif()