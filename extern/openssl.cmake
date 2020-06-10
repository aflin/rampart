set(OPENSSL_SOURCE_DIR ${EXTERN_DIR}/openssl)
set(OPENSSL_TARGET_DIR ${CMAKE_BINARY_DIR}/openssl)
set(OPENSSL_LIBS 
  "${CMAKE_BINARY_DIR}/openssl/lib/libssl.a"
  "${CMAKE_BINARY_DIR}/openssl/lib/libcrypto.a"
)

execute_process(
  COMMAND ./config shared --api=1.0.0 enable-weak-ssl-ciphers enable-ssl2 enable-ssl3 enable-ssl3-method --prefix=${OPENSSL_TARGET_DIR}
  WORKING_DIRECTORY ${OPENSSL_SOURCE_DIR}
  RESULT_VARIABLE CONFIGURE_RESULT
)
if(NOT CONFIGURE_RESULT EQUAL "0")
  message(FATAL_ERROR "configure failed in ${OPENSSL_SOURCE_DIR}")
endif()

execute_process(
  COMMAND make install
  WORKING_DIRECTORY ${OPENSSL_SOURCE_DIR}
  RESULT_VARIABLE MAKE_RESULT
)
if(NOT MAKE_RESULT EQUAL "0")
  message(FATAL_ERROR "make failed in ${OPENSSL_SOURCE_DIR}")
endif()