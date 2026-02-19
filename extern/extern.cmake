set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)


add_subdirectory(${EXTERN_DIR}/texis)

target_compile_definitions(
    texisapi PRIVATE
    RAMPART_INCLUDE_TEXIS_USERFUNC="${CMAKE_SOURCE_DIR}/src/duktape/modules/sql-userfunc.c"
)

add_subdirectory(${EXTERN_DIR}/openssl)

add_subdirectory(${EXTERN_DIR}/oniguruma)

if(WIN32 AND MINGW)
  # GCC 15 defaults to C23 where () means (void), causing curl's cmake
  # check for ioctlsocket to fail. Force the correct result.
  set(HAVE_IOCTLSOCKET_FIONBIO 1 CACHE INTERNAL "")
  set(HAVE_IOCTLSOCKET_CAMEL_FIONBIO 0 CACHE INTERNAL "")
  # Don't build the curl executable on MinGW, only the library
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
endif()
if(CYGWIN)
  # GCC 15 defaults to C23 where 0 is not a null pointer constant,
  # causing curl's cmake recv/send checks to fail. Force POSIX signatures.
  set(curl_cv_recv 1 CACHE INTERNAL "")
  set(curl_cv_func_recv_args "int,void *,size_t,int,ssize_t" CACHE INTERNAL "")
  set(RECV_TYPE_ARG1 "int" CACHE INTERNAL "")
  set(RECV_TYPE_ARG2 "void *" CACHE INTERNAL "")
  set(RECV_TYPE_ARG3 "size_t" CACHE INTERNAL "")
  set(RECV_TYPE_ARG4 "int" CACHE INTERNAL "")
  set(RECV_TYPE_RETV "ssize_t" CACHE INTERNAL "")
  set(HAVE_RECV 1 CACHE INTERNAL "")
  set(curl_cv_send 1 CACHE INTERNAL "")
  set(curl_cv_func_send_args "int,void *,size_t,int,ssize_t,const" CACHE INTERNAL "")
  set(SEND_TYPE_ARG1 "int" CACHE INTERNAL "")
  set(SEND_TYPE_ARG2 "void *" CACHE INTERNAL "")
  set(SEND_TYPE_ARG3 "size_t" CACHE INTERNAL "")
  set(SEND_TYPE_ARG4 "int" CACHE INTERNAL "")
  set(SEND_TYPE_RETV "ssize_t" CACHE INTERNAL "")
  set(SEND_QUAL_ARG2 "const" CACHE INTERNAL "")
  set(HAVE_SEND 1 CACHE INTERNAL "")
endif()
add_subdirectory(${EXTERN_DIR}/curl)

include_directories(${CMAKE_BINARY_DIR}/extern/oniguruma/include)

add_subdirectory(${EXTERN_DIR}/libevent)

add_subdirectory(${EXTERN_DIR}/libevhtp_ws)

add_subdirectory(${EXTERN_DIR}/tidy-html5)

add_subdirectory(${EXTERN_DIR}/cmark)

add_subdirectory(${EXTERN_DIR}/robotstxt)
