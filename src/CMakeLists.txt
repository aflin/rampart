set(CMAKE_BUILD_TYPE Debug)

# for robotstxt on macos
set(CMAKE_CXX_STANDARD 11)

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

add_compile_options(-Wall -g -O2 -std=c99)

add_executable(rampart
	duktape/register.c
	duktape/core/duktape.c
	duktape/core/duk_console.c
	duktape/core/module.c
	duktape/globals/rampart-utils.c
	duktape/globals/rampart-event.c
	duktape/globals/rampart-import.c
	duktape/globals/csv_parser.c
	duktape/globals/hash_random/cityhash.c
	duktape/globals/hash_random/fast_random.c
	duktape/globals/hash_random/murmurhash.c
	linenoise.c
	whereami.c
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
	${PROJECT_SOURCE_DIR}/src/include
	${PROJECT_BINARY_DIR}/texis/texisapi 
	${PROJECT_BINARY_DIR}/texis 
	${PROJECT_SOURCE_DIR}/texis/include 
	${LIBEVENT_TARGET_DIR}/include
	${PROJECT_SOURCE_DIR}/extern/libevent/include
	${PROJECT_BINARY_DIR}/texis/contrib/jansson-2.12/include/
	${PROJECT_SOURCE_DIR}/src/duktape/globals/hash_random/
)

#this is good for later. Right now it interrupts work flow
#set (CMAKE_CURRENT_BINARY_DIR ${PROJECT_BINARY_DIR}/bin )


########### ADD MODULES HERE ###############

## allow linked rampart-crypto.so to be loaded from rampart-server.so in the same dir
SET(CMAKE_SKIP_BUILD_RPATH FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "")
#SET(CMAKE_INSTALL_RPATH "./;${CMAKE_INSTALL_PREFIX}/modules/")


if (NOT CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.0)
###ROBOTS###
add_library(rampart-robots SHARED
   duktape/modules/robot-wrapper.cc
   duktape/modules/rampart-robots.c
)

target_link_libraries(rampart-robots

    ${PROJECT_BINARY_DIR}/extern/robotstxt/librobots.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/strings/libabsl_strings.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/base/libabsl_base.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/base/libabsl_spinlock_wait.a 
    pthread 
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/strings/libabsl_strings_internal.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/numeric/libabsl_int128.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/base/libabsl_throw_delegate.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/base/libabsl_raw_logging_internal.a
    ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-build/absl/base/libabsl_log_severity.a
)

target_include_directories(rampart-robots PRIVATE
     ${PROJECT_SOURCE_DIR}/extern/robotstxt/ 
     ${PROJECT_SOURCE_DIR}/src/duktape/core
     ${PROJECT_SOURCE_DIR}/src
     ${PROJECT_BINARY_DIR}/extern/robotstxt/libs/abseil-cpp-src/
)
set_target_properties(rampart-robots PROPERTIES LINKER_LANGUAGE CXX )
endif()


###CMARK###
add_library(rampart-cmark SHARED
	duktape/modules/rampart-cmark.c
)
target_link_libraries(rampart-cmark
	${PROJECT_BINARY_DIR}/extern/cmark/src/libcmark.a
)
target_include_directories(rampart-cmark PRIVATE
	${PROJECT_SOURCE_DIR}/extern/cmark/src/
	${PROJECT_BINARY_DIR}/extern/cmark/src/
)

###HTML###
add_library(rampart-html SHARED
	duktape/modules/rampart-html.c
)
target_link_libraries(rampart-html
	${PROJECT_BINARY_DIR}/extern/tidy-html5/libtidys.a
)
target_include_directories(rampart-html PRIVATE
	${PROJECT_SOURCE_DIR}/extern/tidy-html5/include/
	${PROJECT_SOURCE_DIR}/extern/tidy-html5/src/
)

###LMDB###

add_library(rampart-lmdb SHARED
 duktape/modules/rampart-lmdb.c
)
target_link_libraries(rampart-lmdb
	${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
) 

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
	COMMAND make
	COMMAND mkdir -p ${PROJECT_BINARY_DIR}/extern/lmdb
	COMMAND find './' -type f  -perm 0755 -exec mv {} "${PROJECT_BINARY_DIR}/extern/lmdb" "\;"
	COMMAND mv liblmdb.a ${PROJECT_BINARY_DIR}/extern/lmdb/
	COMMAND make clean
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/extern/lmdb/
)

add_custom_target(liblmdb
	DEPENDS ${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
)


add_dependencies(rampart-lmdb liblmdb)

###SQL###
add_library(rampart-sql SHARED
 duktape/modules/rampart-sql.c
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

add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/src/texislockd
	COMMAND cp "${PROJECT_BINARY_DIR}/texis/apps/texislockd" ${CMAKE_BINARY_DIR}/src
)

add_custom_target(texislockd
	DEPENDS ${CMAKE_BINARY_DIR}/src/texislockd
)

add_dependencies(rampart-sql texis texislockd)


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
  duktape/modules/rampart-crypto.c  
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
 duktape/modules/rampart-server.c
)
target_link_libraries(rampart-server 
	${LIBEVHTP_TARGET_DIR}/libevhtp.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_openssl.a
  	rampart-crypto
	${ONIGURUMA_TARGET_DIR}/libonig.a
	${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
) 

set_property(TARGET rampart-server PROPERTY IMPORTED_LOCATION "./rampart-crypto.so") 

target_include_directories(rampart-server PRIVATE
	${PROJECT_BINARY_DIR}/extern/openssl/include/
	${PROJECT_SOURCE_DIR}/extern/libevent/include/
	${PROJECT_BINARY_DIR}/extern/libevent/include/
	${PROJECT_SOURCE_DIR}/extern/libevhtp/include/
	${PROJECT_BINARY_DIR}/extern/libevhtp/include/
	${PROJECT_BINARY_DIR}/extern/libdeflate/
)

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
	COMMAND make
	COMMAND mkdir -p ${PROJECT_BINARY_DIR}/extern/libdeflate
	COMMAND find './' -type f  -perm 0755 -regex '.*[^\.b][^sa][^ht]' -exec mv {} "${PROJECT_BINARY_DIR}/extern/libdeflate" "\;"
	COMMAND mv libdeflate.a ${PROJECT_BINARY_DIR}/extern/libdeflate/
	COMMAND cp libdeflate.h ${PROJECT_BINARY_DIR}/extern/libdeflate/
	COMMAND make clean
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/extern/libdeflate/
)

add_custom_target(libdeflate
	DEPENDS ${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
)

add_dependencies(rampart-server libdeflate)



###CURL###
add_library(rampart-curl SHARED
 duktape/modules/rampart-curl.c
)
target_link_libraries(rampart-curl PRIVATE
	${CURL_TARGET_DIR}/lib/libcurl.a
	rampart-crypto
	idn2 ldap z
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
  set_target_properties(rampart-curl rampart-server rampart-sql rampart-redis rampart-crypto rampart-html rampart-cmark rampart-robots rampart-lmdb #example_name
          PROPERTIES 
          LINK_FLAGS   "-undefined dynamic_lookup -Wl,-rpath,./ -Wl,-rpath,${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/"
          SUFFIX       ".so"
          PREFIX ""
  )
else()
  if (NOT CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.0)
    set_target_properties(rampart-curl rampart-server rampart-sql rampart-redis rampart-crypto rampart-html rampart-cmark rampart-robots rampart-lmdb #example_name
            PROPERTIES 
            LINK_FLAGS "-Wl,-rpath ./ -Wl,-rpath,${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/"
            SUFFIX       ".so"
            PREFIX ""
    )
  else()
    set_target_properties(rampart-curl rampart-server rampart-sql rampart-redis rampart-crypto rampart-html rampart-cmark rampart-lmdb #example_name
            PROPERTIES 
            LINK_FLAGS "-Wl,-rpath ./ -Wl,-rpath,${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/"
            SUFFIX       ".so"
            PREFIX ""
    )
  endif()
endif()

## some examples into src temporarily 
set(EXAMPLE_FILES
	"web_server"
	"wikipedia_search"
	"metasearch"
)
foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
  file(COPY ${CMAKE_SOURCE_DIR}/examples/${EXAMPLE_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()

## tests into build/src dir as well (temporary)
file(GLOB TEST_FILES "${CMAKE_SOURCE_DIR}/test/*.js" "${CMAKE_SOURCE_DIR}/test/wiki_00")
foreach(TEST_FILE ${TEST_FILES})
file(COPY ${TEST_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()



install(TARGETS rampart DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

if (NOT CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.0)
install(TARGETS
	rampart-server
	rampart-crypto
	rampart-curl
	rampart-sql
	rampart-redis
	rampart-html
	rampart-cmark
	rampart-robots
	rampart-lmdb
	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/"
)
else()
install(TARGETS
	rampart-server
	rampart-crypto
	rampart-curl
	rampart-sql
	rampart-redis
	rampart-html
	rampart-cmark
	rampart-lmdb
	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/"
)
endif()

#install(CODE "execute_process( \
#    COMMAND ${CMAKE_COMMAND} -E create_symlink \
#    \"${CMAKE_INSTALL_PREFIX}/bin/rampart\" \
#    \"/usr/local/bin/rampart\"   \
#    )"
#)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_BINARY_DIR}/src/babel.js\" \
    \"${PROJECT_BINARY_DIR}/src/babel-polyfill.js\" \
    \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/\"   \
    )"
)

# include files
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/include")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/src/include/duktape.h\" \
    \"${PROJECT_SOURCE_DIR}/src/include/duk_config.h\" \
    \"${PROJECT_SOURCE_DIR}/src/include/rampart.h\" \
    \"${CMAKE_INSTALL_PREFIX}/include\"   \
    )"
)

# test files
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/share/rampart/test")
if (NOT CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.0)
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/test/babel-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/crypto-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/combo-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/curl-server-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/math.js\" \
    \"${PROJECT_SOURCE_DIR}/test/redis-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/robots-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/rputils-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/sql-extras-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/wiki_00\" \
    \"${CMAKE_INSTALL_PREFIX}/share/rampart/test\"   \
    )"
)
else()
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/test/babel-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/crypto-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/combo-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/curl-server-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/math.js\" \
    \"${PROJECT_SOURCE_DIR}/test/redis-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/rputils-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/sql-extras-test.js\" \
    \"${PROJECT_SOURCE_DIR}/test/wiki_00\" \
    \"${CMAKE_INSTALL_PREFIX}/share/rampart/test\"   \
    )"
)
endif()

install(CODE "execute_process ( \
    COMMAND bash -c \"chmod 777 ${CMAKE_INSTALL_PREFIX}/share/rampart/test\"
 )"
)

#js modules
install(CODE "execute_process( \
   COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/jsmodules/rampart-letsencrypt.js\" \
    \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules\"   \
    )"
)

#tsql etc
install(CODE "execute_process( \
   COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_BINARY_DIR}/texis/apps/tsql\" \
    \"${PROJECT_BINARY_DIR}/texis/apps/metamorph\" \
    \"${PROJECT_BINARY_DIR}/texis/apps/rex\" \
    \"${PROJECT_BINARY_DIR}/texis/apps/texislockd\" \
    \"${PROJECT_BINARY_DIR}/texis/apps/backref\" \
    \"${CMAKE_INSTALL_PREFIX}/bin\"   \
    )"
)

# example dirs
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/share/rampart/examples")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_SOURCE_DIR}/examples\" \
    \"${CMAKE_INSTALL_PREFIX}/share/rampart/examples\"   \
    )"
)

# docs dirs
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/share/rampart/docs")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_SOURCE_DIR}/docs/build/html\" \
    \"${CMAKE_INSTALL_PREFIX}/share/rampart/docs\"   \
    )"
)

install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/rampart-server.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/rampart-crypto.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/rampart-curl.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/rampart-sql.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/rampart-lmdb.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/lib/rampart_modules/rampart-redis.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/rampart\")")

