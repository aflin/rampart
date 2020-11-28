set(CMAKE_BUILD_TYPE Debug)

if(${CMAKE_INSTALL_PREFIX})
	message("install = ${CMAKE_INSTALL_PREFIX}")
else()
	set (CMAKE_INSTALL_PREFIX "/usr/local/rampart")
	message("setting install = ${CMAKE_INSTALL_PREFIX}")
endif()

file(GLOB JSMOD_FILES "${PROJECT_SOURCE_DIR}/src/duktape/js_modules/*.js")

foreach(JSMOD_FILE ${JSMOD_FILES})
file(COPY ${JSMOD_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()

set(LIBEVENT_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/libevent)
set(OPENSSL_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/openssl)
set(ONIGURUMA_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/oniguruma)
set(CURL_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/curl)
set(LIBEVHTP_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/libevhtp)

add_definitions(
	-DRP_USING_DUKTAPE 
	-DDUK_CMDLINE_CONSOLE_SUPPORT 
	-DDUK_CMDLINE_MODULE_SUPPORT 
	-DDUK_CMDLINE_FANCY
	-DDUK_CMDLINE_FILEIO 
	-DDUK_DB_SUPPORT
	-D_XOPEN_SOURCE 
	-D_GNU_SOURCE 
	-D_LARGEFILE_SOURCE 
	-D_LARGEFILE64_SOURCE 
	-D_FILE_OFFSET_BITS=64
	-DRP_INST_PATH="${CMAKE_INSTALL_PREFIX}/"
)

#this doesn't work: set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -pedantic -std=c99 -Wall -fstrict-aliasing -fomit-frame-pointer -rdynamic")
# add_compile_options( -O2 -g -pedantic -std=c99 -fstrict-aliasing)
add_compile_options(-Wall -g -O2)


add_executable(rampart
	duktape/register.c
	duktape/core/duktape.c
	duktape/core/duk_console.c
	duktape/core/module.c
	duktape/globals/rampart-utils.c
	duktape/globals/rampart-import.c
	duktape/globals/csv_parser.c
#	duktape/globals/printf.c

	cmdline.c
)

set_target_properties(rampart PROPERTIES
  ENABLE_EXPORTS 1
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

target_link_libraries(rampart LINK_PUBLIC 
	-Wl,-all_load 
	${LIBEVENT_TARGET_DIR}/lib/libevent_core.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_pthreads.a 
	-Wl,-noall_load 
	m dl pthread
)

else()

target_link_libraries(rampart LINK_PUBLIC 
	-Wl,--whole-archive
	${LIBEVENT_TARGET_DIR}/lib/libevent_core.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_pthreads.a 
	-Wl,--no-whole-archive
	m dl pthread
)

endif()

include_directories(
	${PROJECT_BINARY_DIR}/texis/texisapi 
	${PROJECT_BINARY_DIR}/texis 
	${PROJECT_SOURCE_DIR}/texis/include 
	${LIBEVENT_TARGET_DIR}/include
	${PROJECT_SOURCE_DIR}/extern/libevent/include
)

#this is good for later. Right now it interrupts work flow
#set (CMAKE_CURRENT_BINARY_DIR ${PROJECT_BINARY_DIR}/bin )


########### ADD MODULES HERE ###############

## allow linked rampart-crypto.so to be loaded from rampart-server.so in the same dir
SET(CMAKE_SKIP_BUILD_RPATH FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "./;${CMAKE_INSTALL_PREFIX}/modules/")


###SQL###
add_library(rampart-sql SHARED
 duktape/modules/db.c
)
target_link_libraries(rampart-sql
	${PROJECT_BINARY_DIR}/texis/texisapi/libtexisapi.a
	${PROJECT_BINARY_DIR}/texis/mmapi/libmmapi3.a
	z
	${PROJECT_BINARY_DIR}/texis/thirdparty/re2/libre2.a
	pthread
	m
	${PROJECT_BINARY_DIR}/texis/jansson-2.12/src/jansson-2.12-build/lib/libjansson.a
) 
add_dependencies(rampart-sql texis)

# libre2 needs to be linked by c++
set_target_properties(rampart-sql PROPERTIES LINKER_LANGUAGE CXX )

###RAMIS###
add_library(rampart-redis SHARED
	duktape/modules/rampart-redis.c
	redis/resp_client.c
	redis/resp_protocol.c
)

##CRYPTO##
add_library(rampart-crypto SHARED 
  duktape/modules/crypto.c  
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

target_link_libraries(rampart-crypto PRIVATE
	-Wl,-all_load
	${OPENSSL_TARGET_DIR}/ssl/libssl.a 
	${OPENSSL_TARGET_DIR}/crypto/libcrypto.a
	-Wl,-noall_load
)

else()

target_link_libraries(rampart-crypto PRIVATE
	-Wl,--whole-archive
	${OPENSSL_TARGET_DIR}/ssl/libssl.a 
	${OPENSSL_TARGET_DIR}/crypto/libcrypto.a
	-Wl,--no-whole-archive
)

endif()

target_include_directories(rampart-crypto PRIVATE
  ${PROJECT_BINARY_DIR}/extern/openssl/include/
)

###SERVER###
add_library(rampart-server SHARED
 duktape/modules/server.c
)
target_link_libraries(rampart-server 
	${LIBEVHTP_TARGET_DIR}/libevhtp.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_openssl.a
  	rampart-crypto
	${ONIGURUMA_TARGET_DIR}/libonig.a
) 

set_property(TARGET rampart-server PROPERTY IMPORTED_LOCATION "./rampart-crypto.so") 

target_include_directories(rampart-server PRIVATE
	${PROJECT_BINARY_DIR}/extern/openssl/include/
	${PROJECT_SOURCE_DIR}/extern/libevent/include/
	${PROJECT_BINARY_DIR}/extern/libevent/include/
	${PROJECT_SOURCE_DIR}/extern/libevhtp/include/
	${PROJECT_BINARY_DIR}/extern/libevhtp/include/
)

###CURL###
add_library(rampart-curl SHARED
 duktape/modules/curl.c
)
target_link_libraries(rampart-curl PRIVATE
	${CURL_TARGET_DIR}/lib/libcurl.a
	rampart-crypto
	z
)
target_include_directories(rampart-curl PRIVATE
	${PROJECT_SOURCE_DIR}/extern/curl/include/
	${PROJECT_BINARY_DIR}/extern/openssl/include/
)


###### EXAMPLE USER MODULE######
#add_library(example_name SHARED
# example.c
#)
#target_link_libraries(example_name
#	/path/to/library/file.a
#)
#target_include_directories(example_name PRIVATE
#	/path/to/include1
#	/path/to/include2
#)

##############  ADD MODULE NAMES HERE  ##############

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
set_target_properties(rampart-curl rampart-server rampart-sql rampart-redis rampart-crypto #example_name
	PROPERTIES 
	LINK_FLAGS   "-undefined dynamic_lookup" 
	SUFFIX       ".so"
	PREFIX ""
)
else()
set_target_properties(rampart-curl rampart-server rampart-sql rampart-redis rampart-crypto #example_name
	PROPERTIES 
	SUFFIX       ".so"
	PREFIX ""
)
endif()

install(TARGETS rampart DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS
	rampart-server
	rampart-crypto
	rampart-curl
	rampart-sql
	rampart-redis
	DESTINATION "${CMAKE_INSTALL_PREFIX}/modules/"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E create_symlink \
    \"${CMAKE_INSTALL_PREFIX}/bin/rampart\" \
    \"/usr/local/bin/rampart\"   \
    )"
)
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E create_symlink \
    \"${CMAKE_INSTALL_PREFIX}/bin/rampart\" \
    \"/usr/local/bin/rp\"   \
    )"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_BINARY_DIR}/src/babel.js\" \
    \"${PROJECT_BINARY_DIR}/src/babel-polyfill.js\" \
    \"${CMAKE_INSTALL_PREFIX}/modules/\"   \
    )"
)

install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/examples/sample-server")

set(INSTALL_EXAMPLE_DIRS
        "mPurpose"
        "tetris-tutorial"
        "servermods"
)

foreach(INSTALL_EXAMPLE_DIR ${INSTALL_EXAMPLE_DIRS})
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_BINARY_DIR}/src/${INSTALL_EXAMPLE_DIR}\" \
    \"${CMAKE_INSTALL_PREFIX}/examples/sample-server/${INSTALL_EXAMPLE_DIR}\"   \
    )"
)
endforeach()

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_BINARY_DIR}/src/server.js\" \
    \"${PROJECT_BINARY_DIR}/src/servermod.js\" \
    \"${CMAKE_INSTALL_PREFIX}/examples/sample-server\"   \
    )"
)



install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_BINARY_DIR}/src/wikipedia_search\" \
    \"${CMAKE_INSTALL_PREFIX}/examples/wikipedia_search\"   \
    )"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_BINARY_DIR}/src/metasearch\" \
    \"${CMAKE_INSTALL_PREFIX}/examples/metasearch\"   \
    )"
)

install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-server.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-crypto.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-curl.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-sql.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-redis.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/rampart\")")


#this is good for later. Right now it interrupts work flow
#set_target_properties(rampart-curl rampart-utils rampart-server rp
#	PROPERTIES
#	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#)
