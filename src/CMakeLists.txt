set(CMAKE_BUILD_TYPE Debug)

# for robotstxt on macos
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)

if(${CMAKE_INSTALL_PREFIX})
	message("install = ${CMAKE_INSTALL_PREFIX}")
else()
	set (CMAKE_INSTALL_PREFIX "/usr/local/rampart")
	message("setting install = ${CMAKE_INSTALL_PREFIX}")
endif()

execute_process (
	COMMAND bash -c "which gmake"
	OUTPUT_VARIABLE MAKE
)

if (MAKE STREQUAL "")
execute_process (
        COMMAND bash -c "which make"
        OUTPUT_VARIABLE MAKE
)
message("gmake NOT FOUND, using ${MAKE}")
else()
message("make = ${MAKE}")
endif()



set(LIBEVENT_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/libevent)
set(OPENSSL_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/openssl)
set(ONIGURUMA_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/oniguruma)
set(CURL_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/curl)
set(LIBEVHTP_TARGET_DIR ${CMAKE_BINARY_DIR}/extern/libevhtp_ws)

add_definitions(
	-DRP_USING_DUKTAPE
	-D_XOPEN_SOURCE
	-D_GNU_SOURCE
	-D_LARGEFILE_SOURCE
	-D_LARGEFILE64_SOURCE
	-D_FILE_OFFSET_BITS=64
	-DRP_INST_PATH="${CMAKE_INSTALL_PREFIX}/"
)

add_compile_options(-Wall -g -O2)

if (RP_SANITIZE)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    link_libraries("-fsanitize=address")
endif (RP_SANITIZE)

execute_process(
    COMMAND bash -c "if [ -e /usr/local/rampart-build ]; then cat /usr/local/rampart-build ; else uname -a; fi"
    OUTPUT_VARIABLE BUILD_PLATFORM
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
#https://stackoverflow.com/questions/24292898/compile-date-and-time-using-cmake
make_directory(${CMAKE_BINARY_DIR}/include)
FILE (WRITE ${CMAKE_BINARY_DIR}/include/timestamp.cmake "STRING(TIMESTAMP TIMEZ UTC)\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/include/timestamp.cmake "FILE(WRITE ${CMAKE_BINARY_DIR}/include/rampart_timestamp.h \"#ifndef TIMESTAMP_H\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/include/timestamp.cmake "FILE(APPEND ${CMAKE_BINARY_DIR}/include/rampart_timestamp.h \"#define TIMESTAMP_H\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/include/timestamp.cmake "FILE(APPEND ${CMAKE_BINARY_DIR}/include/rampart_timestamp.h \"#define RAMPART_VERSION_TIMESTAMP \\\"\${TIMEZ}\\\"\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/include/timestamp.cmake "FILE(APPEND ${CMAKE_BINARY_DIR}/include/rampart_timestamp.h \"#define RAMPART_BUILD_PLATFORM \\\"${BUILD_PLATFORM}\\\"\\n\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/include/timestamp.cmake "FILE(APPEND ${CMAKE_BINARY_DIR}/include/rampart_timestamp.h \"#endif // TIMESTAMP_H\\n\")\n")
ADD_CUSTOM_TARGET (
    timestamp
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/include/timestamp.cmake
    ADD_DEPENDENCIES ${CMAKE_BINARY_DIR}/include/timestamp.cmake
)


add_executable(rampart
    ${PROJECT_SOURCE_DIR}/extern/utf8proc/utf8proc.c
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/transpiler.c
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/tree-sitter-javascript/src/parser.c
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/tree-sitter-javascript/src/scanner.c
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/lib/src/lib.c
	duktape/register.c
	duktape/core/duktape.c
	duktape/core/module.c
    ${PROJECT_SOURCE_DIR}/extern/texis/texisapi/npmp.c
	duktape/globals/rampart-utils.c
	duktape/globals/colorspace.c
	duktape/globals/jcolor.c
	duktape/globals/rampart-event.c
	duktape/globals/rampart-import.c
	duktape/globals/rampart-thread.c
	duktape/globals/rp_tz.c
	duktape/globals/csv_parser.c
	duktape/globals/hash_random/cityhash.c
	duktape/globals/hash_random/fast_random.c
	duktape/globals/hash_random/murmurhash.c
	duktape/globals/hash_random/hyperloglog.c
	linenoise.c
	whereami.c
	setproctitle.c
	version.c
	cmdline.c
)


set_target_properties(rampart PROPERTIES
  ENABLE_EXPORTS 1
)

add_dependencies(rampart texisapi timestamp)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

target_link_libraries(rampart LINK_PUBLIC
	-Wl,-all_load
	${LIBEVENT_TARGET_DIR}/lib/libevent_core.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_pthreads.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_extra.a
	m dl pthread
)

else()

target_link_libraries(rampart LINK_PUBLIC
	-Wl,--whole-archive
	${LIBEVENT_TARGET_DIR}/lib/libevent_core.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_pthreads.a
	${LIBEVENT_TARGET_DIR}/lib/libevent_extra.a
	-Wl,--no-whole-archive
	m dl pthread util
)

endif()

add_dependencies(rampart timestamp)

target_include_directories(rampart PRIVATE
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/lib/include/
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/lib/src/
    ${PROJECT_SOURCE_DIR}/extern/tree-sitter/
)

include_directories(
	${PROJECT_SOURCE_DIR}/src/include
	${PROJECT_BINARY_DIR}/extern/texis/texisapi
	${PROJECT_BINARY_DIR}/extern/texis
	${PROJECT_SOURCE_DIR}/extern/texis/include
	${LIBEVENT_TARGET_DIR}/include
	${PROJECT_SOURCE_DIR}/extern/libevent/include
	${PROJECT_BINARY_DIR}/extern/texis/contrib/jansson-2.12/include/
	${PROJECT_BINARY_DIR}/include/
	${PROJECT_SOURCE_DIR}/src/duktape/globals/hash_random/
)
########### ADD MODULES HERE ###############

## allow linked rampart-crypto.so to be loaded from rampart-server.so in the same dir
SET(CMAKE_SKIP_BUILD_RPATH FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "")
#SET(CMAKE_INSTALL_RPATH "./;${CMAKE_INSTALL_PREFIX}/modules/")

###ROBOTS###
add_library(rampart-robots SHARED
   duktape/modules/robot-wrapper.cc
   duktape/modules/rampart-robots.c
)

target_link_libraries(rampart-robots

    ${PROJECT_BINARY_DIR}/extern/robotstxt/librobots.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/strings/libabsl_strings.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/base/libabsl_base.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/base/libabsl_spinlock_wait.a
#    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/strings/libabsl_string_view.a
    pthread
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/strings/libabsl_strings_internal.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/numeric/libabsl_int128.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/base/libabsl_throw_delegate.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/base/libabsl_raw_logging_internal.a
    ${PROJECT_BINARY_DIR}/extern/texis/thirdparty/abseil-cpp/absl/base/libabsl_log_severity.a
)

target_include_directories(rampart-robots PRIVATE
     ${PROJECT_SOURCE_DIR}/extern/robotstxt/
     ${PROJECT_SOURCE_DIR}/src/duktape/core
     ${PROJECT_SOURCE_DIR}/src
     ${PROJECT_SOURCE_DIR}/extern/texis/thirdparty/abseil-cpp/
)

set_target_properties(rampart-robots PROPERTIES LINKER_LANGUAGE CXX )

add_dependencies(rampart-robots robots robots-static robots-main)


###ALMANAC###
add_library(rampart-almanac SHARED
	duktape/modules/rampart-almanac.c
    duktape/modules/almanac.c
    duktape/modules/astronomy.c
)

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
add_dependencies(rampart-cmark cmark_static)

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
add_dependencies(rampart-html tidy)

###LMDB###

add_library(rampart-lmdb SHARED
 duktape/modules/rampart-lmdb.c
)
target_link_libraries(rampart-lmdb
	${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
)

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
	COMMAND ${MAKE} XCFLAGS=-fPIC
	WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/extern/lmdb/
)

add_custom_target(liblmdb
	DEPENDS ${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
)

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/extern/lmdb/
	COMMAND cp -a ${PROJECT_SOURCE_DIR}/extern/lmdb ${PROJECT_BINARY_DIR}/extern/
)

add_custom_target(lmdb_dir
	DEPENDS ${PROJECT_BINARY_DIR}/extern/lmdb/
)

add_dependencies(liblmdb lmdb_dir)
add_dependencies(rampart-lmdb liblmdb)

###SQL###
add_library(rampart-sql SHARED
 duktape/modules/rampart-sql.c
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

    target_link_libraries(rampart-sql
        ${PROJECT_BINARY_DIR}/extern/texis/texisapi/libtexisapi.a
        z
        re2
        pthread
        m
        ${PROJECT_BINARY_DIR}/extern/texis/jansson-2.12/src/jansson-2.12-build/lib/libjansson.a
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")

    target_link_libraries(rampart-sql
        ${PROJECT_BINARY_DIR}/extern/texis/texisapi/libtexisapi.a
        z
        re2
        pthread
        m
        kvm
        ${PROJECT_BINARY_DIR}/extern/texis/jansson-2.12/src/jansson-2.12-build/lib/libjansson.a
    )

else()

    target_link_libraries(rampart-sql LINK_PUBLIC
        ${PROJECT_BINARY_DIR}/extern/texis/texisapi/libtexisapi.a
        z
        re2
        pthread
        m
        rt
        ${PROJECT_BINARY_DIR}/extern/texis/jansson-2.12/src/jansson-2.12-build/lib/libjansson.a
    )

endif()

add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/src/texislockd
    COMMENT "Copying texislockd"
	COMMAND cp "${PROJECT_BINARY_DIR}/extern/texis/apps/texislockd" ${CMAKE_BINARY_DIR}/src
)

add_custom_target(texislockd-cp 
    DEPENDS ${CMAKE_BINARY_DIR}/src/texislockd
)
add_dependencies(rampart texislockd-cp)
add_dependencies(texislockd-cp rampart-sql)

add_dependencies(rampart-sql texisapi)


# libre2 needs to be linked by c++
set_target_properties(rampart-sql PROPERTIES LINKER_LANGUAGE CXX )

###REDIS###
add_library(rampart-redis SHARED
	duktape/modules/rampart-redis.c
	redis/resp_client.c
	redis/resp_protocol.c
)

##CRYPTO##
add_library(rampart-crypto SHARED
  duktape/modules/rampart-crypto.c
  duktape/modules/rampart-crypto-passwd.c
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

target_link_libraries(rampart-crypto PRIVATE
	-Wl,-all_load
	${OPENSSL_TARGET_DIR}/ssl/libssl.a
	${OPENSSL_TARGET_DIR}/crypto/libcrypto.a
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

add_dependencies(rampart-crypto openssl)

###SERVER###
add_library(rampart-server SHARED
 duktape/modules/rampart-server.c
)
target_link_libraries(rampart-server
	${LIBEVHTP_TARGET_DIR}/libevhtp_ws.a
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
	${PROJECT_SOURCE_DIR}/extern/libevhtp_ws/include/
	${PROJECT_BINARY_DIR}/extern/libevhtp_ws/include/
	${PROJECT_BINARY_DIR}/extern/libdeflate/
)



#add_custom_command(
#	OUTPUT ${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
#	COMMAND ${MAKE}
#	COMMAND mkdir -p ${PROJECT_BINARY_DIR}/extern/libdeflate
#	COMMAND find './' -type f  -perm 0755 -regex '.*[^\.b][^sa][^ht]' -exec mv {} "${PROJECT_BINARY_DIR}/extern/libdeflate" "\;"
#	COMMAND mv libdeflate.a ${PROJECT_BINARY_DIR}/extern/libdeflate/
#	COMMAND cp libdeflate.h ${PROJECT_BINARY_DIR}/extern/libdeflate/
#	COMMAND ${MAKE} clean
#	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/extern/libdeflate/
#)

add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/extern/libdeflate
    COMMAND mkdir -p ${PROJECT_BINARY_DIR}/extern
    COMMAND cp -a ${PROJECT_SOURCE_DIR}/extern/libdeflate ${PROJECT_BINARY_DIR}/extern
)

add_custom_target(cp_libdeflate
	DEPENDS ${PROJECT_BINARY_DIR}/extern/libdeflate/
)


add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
	COMMAND ${MAKE} CFLAGS=${CMAKE_C_FLAGS} CPPFLAGS=${CMAKE_CXX_FLAGS} LDFLAGS=${CMAKE_C_FLAGS}
	WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/extern/libdeflate/
)

add_custom_target(libdeflate
	DEPENDS ${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
)

add_dependencies(rampart-server libdeflate)
add_dependencies(libdeflate cp_libdeflate)

###NET###

set(SEARCH_CA_BUNDLE_FILES
    /etc/ssl/certs/ca-certificates.crt
    /etc/pki/tls/certs/ca-bundle.crt
    /usr/share/ssl/certs/ca-bundle.crt
    /usr/local/share/certs/ca-root-nss.crt
    /etc/ssl/cert.pem
)

set(NET_CA_FILE "")
set(NET_CA_PATH "")

foreach(SEARCH_CA_BUNDLE ${SEARCH_CA_BUNDLE_FILES})
  if(EXISTS "${SEARCH_CA_BUNDLE}")
    message(STATUS "Found CA bundle file: ${SEARCH_CA_BUNDLE}")
    set(NET_CA_FILE "${SEARCH_CA_BUNDLE_PATH}")
    break()
  endif()
endforeach()

if(EXISTS "/etc/ssl/certs")
  set(NET_CA_PATH "/etc/ssl/certs")
  message(STATUS "Found CA path at /etc/ssl/certs")
endif()


if("${NET_CA_PATH}" EQUAL "")
    message( FATAL_ERROR "Cannot find a suitable path for certificate authority file")
endif()

add_library(rampart-net SHARED
 duktape/modules/rampart-net.c
)

target_compile_options(rampart-net PRIVATE
    "-DNET_CA_PATH=\"${NET_CA_PATH}\""
    "-DNET_CA_FILE=\"${NET_CA_FILE}\""
)

target_link_libraries(rampart-net
	${LIBEVENT_TARGET_DIR}/lib/libevent_openssl.a
  	rampart-crypto
)

set_property(TARGET rampart-net PROPERTY IMPORTED_LOCATION "./rampart-crypto.so")

target_include_directories(rampart-net PRIVATE
	${PROJECT_BINARY_DIR}/extern/openssl/include/
	${PROJECT_SOURCE_DIR}/extern/libevent/include/
	${PROJECT_BINARY_DIR}/extern/libevent/include/
)


###CURL###
add_library(rampart-curl SHARED
 duktape/modules/rampart-curl.c
)
target_link_libraries(rampart-curl PRIVATE
	${CURL_TARGET_DIR}/lib/libcurl.a
	rampart-crypto
	z
)
target_include_directories(rampart-curl PRIVATE
	${PROJECT_SOURCE_DIR}/extern/curl/include/
	${PROJECT_BINARY_DIR}/extern/openssl/include/
	${PROJECT_BINARY_DIR}/extern/curl/lib/
)

add_dependencies(rampart-curl libcurl rampart-crypto)

###PYTHON###

set(PYTHON_MINOR "11")
set(PYTHON_PATCH "2")
set(PYTHON_DIR_NAME "Python-3.${PYTHON_MINOR}.${PYTHON_PATCH}")
set(PYTHON_BIN_DIR "${PROJECT_BINARY_DIR}/extern/${PYTHON_DIR_NAME}")

add_custom_command(
    OUTPUT ${PYTHON_BIN_DIR}
    COMMAND mkdir -p ${PROJECT_BINARY_DIR}/extern
    COMMAND cp -a ${PROJECT_SOURCE_DIR}/extern/${PYTHON_DIR_NAME} ${PROJECT_BINARY_DIR}/extern
)

add_custom_target(cp_python
	DEPENDS ${PYTHON_BIN_DIR}
)


set(PYFLAGS "-I/usr/local/include -fPIC ${CMAKE_C_FLAGS}")
set(PYLDFLAGS "-L /usr/local/lib")
set(PYPKG "")

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

	execute_process(COMMAND brew --prefix gdbm OUTPUT_VARIABLE GDBM_PREF OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND brew --prefix xz OUTPUT_VARIABLE XZ_PREF OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND brew --prefix tcl-tk OUTPUT_VARIABLE PYTK_PREF OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND brew --prefix readline OUTPUT_VARIABLE RL_PREF OUTPUT_STRIP_TRAILING_WHITESPACE)

	foreach(PREF IN ITEMS ${GDBM_PREF} ${XZ_PREF} ${PYTK_PREF} ${RL_PREF})
		if(NOT "${PREF}" STREQUAL "")
			set(PYFLAGS "${PYFLAGS} -I${PREF}/include")
			set(PYLDFLAGS "${PYLDFLAGS} -L${PREF}/lib")
		endif()
	endforeach()

	set(PYPKG "${PYTK_PREF}/lib/pkgconfig")

endif()

message("Configuration for python build:")
#message("    RAMPART_SSL_DIR=${PROJECT_BINARY_DIR}/extern/openssl PKG_CONFIG_PATH=\"${PYPKG}\" LDFLAGS=\"${PYLDFLAGS}\" CFLAGS=\"${PYFLAGS}\" TCLTK_LIBS=\"-ltk -ltcl\" ./configure --enable-optimizations --prefix=\"${PROJECT_BINARY_DIR}/extern/${PYTHON_DIR_NAME}/build\"")

add_custom_command(
	OUTPUT ${PYTHON_BIN_DIR}/Makefile
	DEPENDS rampart-crypto
	COMMAND bash -c "if [ ! -e ${PYTHON_BIN_DIR}/Makefile ]; then RAMPART_SSL_DIR=${PROJECT_BINARY_DIR}/extern/openssl PKG_CONFIG_PATH=\"${PYPKG}\" LDFLAGS=\"${PYLDFLAGS}\" CPPFLAGS=\"${PYFLAGS}\" TCLTK_LIBS=\"-ltk -ltcl\" ./configure --enable-optimizations --prefix=\"${PROJECT_BINARY_DIR}/extern/${PYTHON_DIR_NAME}/build\";fi"
	WORKING_DIRECTORY ${PYTHON_BIN_DIR}
    VERBATIM
)

add_custom_command(
	DEPENDS ${PYTHON_BIN_DIR}/Makefile
	OUTPUT ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
	COMMAND ${CMAKE_MAKE_PROGRAM} CFLAGS=${CMAKE_C_FLAGS}
	COMMAND ${CMAKE_MAKE_PROGRAM} CFLAGS=${CMAKE_C_FLAGS} install
	WORKING_DIRECTORY ${PYTHON_BIN_DIR}
)

add_custom_target(libpython
	DEPENDS ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
	COMMAND bash -c "rm -f ${PROJECT_BINARY_DIR}/src/python3-lib"
	COMMAND bash -c "ln -s ${PYTHON_BIN_DIR}/build/lib/python3.${PYTHON_MINOR} ${PROJECT_BINARY_DIR}/src/python3-lib"
)

add_library(rampart-python SHARED
	duktape/modules/rampart-python.c
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
	execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE MACOS_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)

	#TODO: find the real cutoff version for requiring libintl.  And maybe why earlier versions of python
	#      need it in the first place
	if (MACOS_VERSION VERSION_LESS "10.12")
		## NO LONGER DOING UNIVERSAL MAC BUILDS, cuz with python, its
		## too difficult
		execute_process(COMMAND brew --prefix gettext OUTPUT_VARIABLE GTXT_PREF OUTPUT_STRIP_TRAILING_WHITESPACE)

		if("${GTXT_PREF}" STREQUAL "")
			message( FATAL_ERROR "gettext library not found. Try 'brew install gettext'")
		endif()

		target_link_libraries(rampart-python PRIVATE
			${GTXT_PREF}/lib/libintl.a
			${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
			pthread dl util m iconv
			"-framework CoreFoundation"
		)
	else()
		target_link_libraries(rampart-python PRIVATE
			${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
			pthread dl util m
		)
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	if(EXISTS "/usr/local/lib/libintl.so")
		set(GTXT "/usr/local/lib/libintl.so")
	else()
                message( FATAL_ERROR "gettext library not found. Try 'pkg install gettext'")
        endif()

        target_link_libraries(rampart-python PRIVATE
                ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
                pthread dl util m
                /usr/local/lib/libintl.so
		"-Wl,-z,notext"
        )

else()
        target_link_libraries(rampart-python PRIVATE
                ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
                pthread dl util m
        )
endif()
target_include_directories(rampart-python PRIVATE
	${PYTHON_BIN_DIR}/build/include/python3.${PYTHON_MINOR}
)

add_dependencies(rampart-python libpython)
add_dependencies(libpython cp_python)
add_dependencies(rampart-python openssl)


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
  set_target_properties(
        rampart-curl rampart-server rampart-sql
        rampart-redis rampart-crypto rampart-html
        rampart-cmark rampart-robots rampart-lmdb
        rampart-net rampart-python rampart-almanac
        #example_name
          PROPERTIES
          LINK_FLAGS   "-undefined dynamic_lookup -Wl,-rpath,./"
          SUFFIX       ".so"
          PREFIX ""
  )
else()
  set_target_properties(
        rampart-curl rampart-server rampart-sql
        rampart-redis rampart-crypto rampart-html
        rampart-cmark rampart-robots rampart-lmdb
        rampart-net rampart-python rampart-almanac
        #example_name
          PROPERTIES
          LINK_FLAGS "-Wl,-rpath,./"
          SUFFIX       ".so"
          PREFIX ""
  )
endif()

## tests and js_mods into build/src dir as well (temporary)
file(GLOB TEST_FILES
    "${CMAKE_SOURCE_DIR}/test/*.js"
    "${CMAKE_SOURCE_DIR}/test/wiki_00"
    "${CMAKE_SOURCE_DIR}/js_modules/*.js"
)
foreach(TEST_FILE ${TEST_FILES})
file(COPY ${TEST_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()



install(TARGETS rampart DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

install(TARGETS
	rampart-server
	rampart-crypto
	rampart-curl
	rampart-sql
	rampart-redis
	rampart-html
	rampart-cmark
	rampart-net
	rampart-robots
	rampart-lmdb
	rampart-python
    rampart-almanac
	DESTINATION "${CMAKE_INSTALL_PREFIX}/modules/"
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/js_modules/
    DESTINATION ${CMAKE_INSTALL_PREFIX}/modules/
    FILES_MATCHING PATTERN "*.js"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/src/install.sh\" \
    \"${CMAKE_INSTALL_PREFIX}/\"   \
    )"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/src/install-helper.js\" \
    \"${CMAKE_INSTALL_PREFIX}/\"   \
    )"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/src/run_tests.sh\" \
    \"${CMAKE_INSTALL_PREFIX}/\"   \
    )"
)

# unsupported dir
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/unsupported_extras/ 
    DESTINATION "${CMAKE_INSTALL_PREFIX}/unsupported_extras"
)

# include files
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/include")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/src/include/duktape.h\" \
    \"${PROJECT_SOURCE_DIR}/src/include/duk_config.h\" \
    \"${PROJECT_SOURCE_DIR}/src/include/rampart.h\" \
    \"${PROJECT_SOURCE_DIR}/src/include/rp_server.h\" \
    \"${CMAKE_INSTALL_PREFIX}/include\"   \
    )"
)

# test files
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/test")

file(GLOB TEST_FILES
    "${CMAKE_SOURCE_DIR}/test/*.js"
    "${CMAKE_SOURCE_DIR}/test/wiki_00"
)

foreach(TEST_FILE ${TEST_FILES})
  install(CODE "execute_process( \
      COMMAND ${CMAKE_COMMAND} -E copy \
      \"${TEST_FILE}\" \
      \"${CMAKE_INSTALL_PREFIX}/test\"   \
      )"
  )
endforeach()

install(CODE "execute_process ( \
    COMMAND bash -c \"chmod 777 ${CMAKE_INSTALL_PREFIX}/test\"
 )"
)

#Rampart License
install(CODE "execute_process( \
   COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/LICENSE\" \
    \"${CMAKE_INSTALL_PREFIX}/\"   \
    )"
)

#tsql etc
install(CODE "execute_process( \
   COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/tsql\" \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/metamorph\" \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/rex\" \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/addtable\" \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/kdbfchk\" \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/texislockd\" \
    \"${PROJECT_BINARY_DIR}/extern/texis/apps/backref\" \
    \"${CMAKE_INSTALL_PREFIX}/bin\"   \
    )"
)

# example dirs
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/examples")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_SOURCE_DIR}/examples\" \
    \"${CMAKE_INSTALL_PREFIX}/examples\"   \
    )"
)

#python stuff

#somehow python file isn't always installed in Python3.11/build/bin
install(CODE "\
    if(NOT EXISTS ${PYTHON_BIN_DIR}/build/bin/python3.${PYTHON_MINOR} )
        if(EXISTS ${PYTHON_BIN_DIR}/python)
            execute_process(COMMAND bash -c \"cp ${PYTHON_BIN_DIR}/python.exe ${PYTHON_BIN_DIR}/build/bin/python3.${PYTHON_MINOR}\")
        elseif(EXISTS ${PYTHON_BIN_DIR}/python.exe)
            execute_process(COMMAND bash -c \"cp ${PYTHON_BIN_DIR}/python ${PYTHON_BIN_DIR}/build/bin/python3.${PYTHON_MINOR}\")
        else()
            message(FATAL_ERROR, \"Could not find the python executable\")
        endif()
	endif()
    "
)


install(CODE "execute_process( \
    COMMAND bash -c \"chmod 755 ${PYTHON_BIN_DIR}/build/bin/python3.${PYTHON_MINOR} \"\
    )"
)
install(CODE "execute_process( \
    COMMAND bash -c \"chmod 755 ${PYTHON_BIN_DIR}/build/bin/pip3 \"\
    )"
)

install(CODE "execute_process( \
    COMMAND bash -c \"mkdir -p \
	${CMAKE_INSTALL_PREFIX}/modules/python \"\
    )"
)

install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy \
    \"${PROJECT_SOURCE_DIR}/src/rebase-python.sh\" \
    \"${CMAKE_INSTALL_PREFIX}/modules/python/\"   \
    )"
)

install(DIRECTORY DESTINATION "${PYTHON_BIN_DIR}/build/lib")
install(CODE "execute_process( \
    COMMAND bash -c \"cp -a \
	${PYTHON_BIN_DIR}/build/bin \
	${PYTHON_BIN_DIR}/build/lib \
	${CMAKE_INSTALL_PREFIX}/modules/python/ \"\
    )"
)

install(
	CODE "execute_process( COMMAND bash \"${CMAKE_INSTALL_PREFIX}/modules/python/rebase-python.sh\")"
)

install(CODE "execute_process( \
    COMMAND bash -c \"rm -rf \
	${CMAKE_INSTALL_PREFIX}/modules/python3-lib \
	${CMAKE_INSTALL_PREFIX}/bin/python3r \
	${CMAKE_INSTALL_PREFIX}/bin/pip3r \"\
    )"
)

install(CODE "execute_process( \
    COMMAND bash -c \"ln -s \
	python/lib/python3.${PYTHON_MINOR} \
	${CMAKE_INSTALL_PREFIX}/modules/python3-lib \"\
    )"
)

install(CODE "execute_process( \
    COMMAND bash -c \"ln -s \
	../modules/python/bin/python3 \
	${CMAKE_INSTALL_PREFIX}/bin/python3r \"\
    )"
)

install(CODE "execute_process( \
    COMMAND bash -c \"ln -s \
	../modules/python/bin/pip3 \
	${CMAKE_INSTALL_PREFIX}/bin/pip3r \"\
    )"
)


install(CODE "execute_process( \
    COMMAND bash -c \"chmod 755 ${CMAKE_INSTALL_PREFIX}/modules/python/bin/python3.${PYTHON_MINOR} \"\
    )"
)
install(CODE "execute_process( \
    COMMAND bash -c \"chmod 755 ${CMAKE_INSTALL_PREFIX}/modules/python/bin/pip3 \"\
    )"
)


# web_server
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/web_server")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_SOURCE_DIR}/web_server\" \
    \"${CMAKE_INSTALL_PREFIX}/web_server\"   \
    )"
)

# licenses
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/licenses")
install(CODE "execute_process( \
    COMMAND ${CMAKE_COMMAND} -E copy_directory \
    \"${PROJECT_SOURCE_DIR}/licenses\" \
    \"${CMAKE_INSTALL_PREFIX}/licenses\"   \
    )"
)

# derivations
install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/derivations")
install(CODE "execute_process( COMMAND bash -c \"cp -a ${PROJECT_SOURCE_DIR}/derivations/* ${CMAKE_INSTALL_PREFIX}/derivations/\")" )

if (CMAKE_UID EQUAL 0)
    install(CODE "execute_process ( \
        COMMAND bash -c \"chown -R nobody ${CMAKE_INSTALL_PREFIX}/web_server\"
      )"
    )
endif()

install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-cmark.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-curl.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-crypto.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-html.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-lmdb.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-net.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-python.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-redis.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-robots.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-server.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-sql.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/rampart-almanac.so\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/rampart\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/tsql\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/addtable\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/kdbfchk\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/metamorph\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/rex\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/bin/texislockd\")")
install(CODE "execute_process(COMMAND strip -S \"${CMAKE_INSTALL_PREFIX}/modules/python/bin/python3\")")
