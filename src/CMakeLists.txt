set(CMAKE_BUILD_TYPE Debug)

add_definitions(-DRP_USING_DUKTAPE -DDUK_CMDLINE_PRINTALERT_SUPPORT -DDUK_CMDLINE_CONSOLE_SUPPORT -DDUK_CMDLINE_LOGGING_SUPPORT -DDUK_CMDLINE_MODULE_SUPPORT -DDUK_CMDLINE_FILEIO -DDUK_DB_SUPPORT)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -g -pedantic -std=c99 -Wall -fstrict-aliasing -fomit-frame-pointer -rdynamic")


add_executable(rp
	duktape.c
	duk_cmdline.c
	duk_print_alert.c
	duk_console.c
	duk_logging.c
	duk_module_duktape.c
	modules.c
	resp_client.c
	resp_protocol.c
	printf.c
	db.c )

set_target_properties(rp PROPERTIES
  ENABLE_EXPORTS 1
)

target_link_libraries(rp LINK_PUBLIC 
	${PROJECT_BINARY_DIR}/texis/texisapi/libtexisapi.a
	${PROJECT_BINARY_DIR}/texis/mmapi/libmmapi3.a
	${PROJECT_BINARY_DIR}/texis/jansson-2.12/src/jansson-2.12-build/lib/libjansson.a
	m z dl pthread
)

#-shared -pthread -fPIC curl.c -lcurl

include_directories(
	${PROJECT_BINARY_DIR}/texis/texisapi 
	${PROJECT_BINARY_DIR}/texis 
	${PROJECT_SOURCE_DIR}/texis/include 
	${PROJECT_SOURCE_DIR}/extern/curl/include/
	${PROJECT_BINARY_DIR}/extern/openssl/include/
)

add_library(rpcurl SHARED
 curl.c
)

add_library(rputils SHARED
 utils.c
)

add_library(rpserver SHARED
 server.c
)

#this is good for later. Right now it interrupts work flow
#set (CMAKE_CURRENT_BINARY_DIR ${PROJECT_BINARY_DIR}/bin )


set(INSTALLFILES
	"server.js"
	"utils.js"
	"dbtest.js"
	"mPurpose"
	"tetris-tutorial"
)

foreach(INSTALLFILE ${INSTALLFILES})
file(COPY ${INSTALLFILE} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

set(LIBEVENT_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/libevent)
set(OPENSSL_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/openssl)
set(ONIGURUMA_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/oniguruma)
set(CURL_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/curl)
set(LIBEVHTP_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/libevhtp)

target_link_libraries(rpcurl 
	${CURL_TARGET_DIR}/lib/libcurl.a
	${OPENSSL_TARGET_DIR}/ssl/libssl.a
	${OPENSSL_TARGET_DIR}/crypto/libcrypto.a
)

target_link_libraries(rpserver 
	${LIBEVHTP_TARGET_DIR}/libevhtp.a
	${LIBEVENT_TARGET_DIR}/lib/libevent.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_openssl.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_pthreads.a 
	${OPENSSL_TARGET_DIR}/ssl/libssl.a 
	${OPENSSL_TARGET_DIR}/crypto/libcrypto.a
	${ONIGURUMA_TARGET_DIR}/libonig.a) 

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
set_target_properties(rpcurl rputils rpserver 
	PROPERTIES 
	LINK_FLAGS   "-undefined dynamic_lookup" 
	SUFFIX       ".so"
	PREFIX ""
)
else()
set_target_properties(rpcurl rputils rpserver 
	PROPERTIES 
	SUFFIX       ".so"
	PREFIX ""
)

endif()

#this is good for later. Right now it interrupts work flow
#set_target_properties(rpcurl rputils rpserver rp
#	PROPERTIES
#	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#)
