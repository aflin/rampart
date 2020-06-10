set(CURL_SOURCE_DIR ${EXTERN_DIR}/curl)
set(CURL_TARGET_DIR ${CMAKE_BINARY_DIR}/curl)
include(${EXTERN_DIR}/autoconf.cmake)

autoreconf(curl ${CURL_SOURCE_DIR})
execute_process(
  COMMAND ./configure --prefix=${CURL_TARGET_DIR}
  WORKING_DIRECTORY ${CURL_SOURCE_DIR}
  RESULT_VARIABLE CONFIGURE_RESULT
)
if(NOT CONFIGURE_RESULT EQUAL "0")
  message(FATAL_ERROR "configure failed in ${CURL_SOURCE_DIR}")
endif()
execute_process( 
  COMMAND make install 
  WORKING_DIRECTORY ${CURL_SOURCE_DIR}
  RESULT_VARIABLE MAKE_RESULT
)
if(NOT MAKE_RESULT EQUAL "0")
  message(FATAL_ERROR "make failed in ${CURL_SOURCE_DIR}")
endif()

