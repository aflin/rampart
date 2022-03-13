set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)

add_subdirectory(${EXTERN_DIR}/openssl)

add_subdirectory(${EXTERN_DIR}/oniguruma)

add_subdirectory(${EXTERN_DIR}/curl)

include_directories(${CMAKE_BINARY_DIR}/extern/oniguruma/include)

add_subdirectory(${EXTERN_DIR}/libevent)

add_subdirectory(${EXTERN_DIR}/libevhtp_ws)

add_subdirectory(${EXTERN_DIR}/tidy-html5)

add_subdirectory(${EXTERN_DIR}/cmark)

add_subdirectory(${EXTERN_DIR}/robotstxt)
