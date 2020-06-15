set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)

if (
	(NOT EXISTS "${CMAKE_BINARY_DIR}/extern/openssl/ssl/libssl.a")
		OR
	(NOT EXISTS "${CMAKE_BINARY_DIR}/extern/openssl/crypto/libcrypto.a")
)
add_subdirectory(${EXTERN_DIR}/openssl)
endif()

if (NOT EXISTS "${CMAKE_BINARY_DIR}/extern/oniguruma/libonig.a")
add_subdirectory(${EXTERN_DIR}/oniguruma)
endif()

if (NOT EXISTS "${CMAKE_BINARY_DIR}/extern/curl/lib/libcurl.a")
add_subdirectory(${EXTERN_DIR}/curl)
endif()

include_directories(${CMAKE_BINARY_DIR}/extern/oniguruma/include)

if (NOT EXISTS "${CMAKE_BINARY_DIR}/extern/libevent/lib/libevent_core.a")
add_subdirectory(${EXTERN_DIR}/libevent)
endif()

if (NOT EXISTS "${CMAKE_BINARY_DIR}/extern/libevhtp/libevhtp.a")
add_subdirectory(${EXTERN_DIR}/libevhtp)
endif()
