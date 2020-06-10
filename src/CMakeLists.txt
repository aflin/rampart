find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(mmapi3 PUBLIC ${MATH_LIBRARY})
endif()
find_library(DYNAMIC_LINK_LIBRARY dl)
if(DYNAMIC_LINK_LIBRARY)
    target_link_libraries(texisapi PUBLIC ${DYNAMIC_LINK_LIBRARY})
endif()
#
# EPI_HAVE_PTHREADS
#
set(THREADS_PREFER_PTHREADS_FLAG ON)
find_package(Threads REQUIRED)
IF(Threads_FOUND)
	SET(EPI_HAVE_PTHREADS Threads_FOUND)
	CHECK_SYMBOL_EXISTS(PTHREAD_MUTEX_INITIALIZER	pthread.h	HAVE_PTHREAD_MUTEX_INITIALIZER)
ENDIF(Threads_FOUND)
#

add_definitions(-DDUK_CMDLINE_PRINTALERT_SUPPORT -DDUK_CMDLINE_CONSOLE_SUPPORT -DDUK_CMDLINE_LOGGING_SUPPORT -DDUK_CMDLINE_MODULE_SUPPORT -DDUK_CMDLINE_FILEIO -DDUK_DB_SUPPORT)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -g -pedantic -std=c99 -Wall -fstrict-aliasing -fomit-frame-pointer -rdynamic")


add_executable(rp
	duktape.c
	duk_cmdline.c
	duk_print_alert.c
	duk_console.c
	duk_logging.c
	duk_module_duktape.c
	modules.c
	db.c )

set_target_properties(rp PROPERTIES
  ENABLE_EXPORTS 1
)


target_link_libraries(rp PUBLIC Threads::Threads)
target_link_libraries(rp LINK_PUBLIC m texisapi mmapi3 z dl)

#-shared -pthread -fPIC curl.c -lcurl

#message("CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/texis/texisapi /usr/local/include /usr/local/opt/curl/include /usr/local/opt/openssl@1.1/include)
link_directories(${PROJECT_BINARY_DIR}/texis/texisapi /usr/local/lib /usr/local/opt/curl/lib /usr/local/opt/openssl@1.1/lib)
else()
include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/texis/texisapi)
link_directories(${PROJECT_BINARY_DIR}/texis/texisapi)
endif()

add_library(rpcurl SHARED
 curl.c
)

add_library(rputils SHARED
 utils.c
)

add_library(rpserver SHARED
 server.c
)

configure_file(server.js ${CMAKE_CURRENT_BINARY_DIR}/server.js COPYONLY)
configure_file(utils.js ${CMAKE_CURRENT_BINARY_DIR}/utils.js COPYONLY)
configure_file(dbtest.js ${CMAKE_CURRENT_BINARY_DIR}/dbtest.js COPYONLY)
file(COPY mPurpose DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/mPurpose)
file(COPY tetris-tutorial DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tetris-tutorial)

set(LIBEVENT_TARGET_DIR ${CMAKE_BINARY_DIR}/libevent)
set(OPENSSL_TARGET_DIR ${CMAKE_BINARY_DIR}/openssl)
set(ONIGURUMA_TARGET_DIR ${CMAKE_BINARY_DIR}/oniguruma)
set(CURL_TARGET_DIR ${CMAKE_BINARY_DIR}/curl)
target_link_libraries(rpcurl 
	${CURL_TARGET_DIR}/lib/libcurl.a
	${OPENSSL_TARGET_DIR}/lib/libssl.a 
	${OPENSSL_TARGET_DIR}/lib/libcrypto.a
	)

target_link_libraries(rpserver 
	${LIBEVHTP_TARGET_DIR}/libevhtp.a
	${LIBEVENT_TARGET_DIR}/lib/libevent.a 
	# ${LIBEVENT_TARGET_DIR}/lib/libevent_extra.a 
	${LIBEVENT_TARGET_DIR}/lib/libevent_openssl.a 
	# ${LIBEVENT_TARGET_DIR}/lib/libevent_core.a 
	${LIBEVENT_TARGET_DIR}/lib/libevent_pthreads.a 
	${OPENSSL_TARGET_DIR}/lib/libssl.a 
	${OPENSSL_TARGET_DIR}/lib/libcrypto.a 
	${ONIGURUMA_TARGET_DIR}/lib/libonig.a 
	pthread)

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
