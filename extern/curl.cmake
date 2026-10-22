# libcurl (static, PIC) for rampart-curl.so, with HTTP/2 via the vendored
# nghttp2.  extern/curl is the unmodified upstream release; everything rampart
# needs is set here.  Included from extern.cmake after openssl.cmake.
#
# Features: OpenSSL (vendored), zlib (gzip only -- brotli/zstd would add
# non-base runtime libraries on macOS/FreeBSD/minimal Linux), HTTP/2, NTLM.
# No libidn2 (LGPL; rampart-curl converts IDN hostnames itself via
# rampart-url), LDAP, libssh2, libpsl or GSSAPI.  The resulting rampart-curl.so needs only rampart-crypto.so, libz
# and libc.

add_subdirectory(${EXTERN_DIR}/nghttp2)

# OpenSSL: point FindOpenSSL at the vendored build.  The archives do not exist
# yet at configure time, so the feature probes curl would run against them
# are pre-answered for the vendored OpenSSL 4.
set(OPENSSL_ROOT_DIR       "${OPENSSL_BUILD_DIR}"        CACHE PATH     "" FORCE)
set(OPENSSL_INCLUDE_DIR    "${OPENSSL_BUILD_DIR}/include" CACHE PATH     "" FORCE)
set(OPENSSL_SSL_LIBRARY    "${OPENSSL_LIB_SSL}"           CACHE FILEPATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_LIB_CRYPTO}"        CACHE FILEPATH "" FORCE)
set(OPENSSL_USE_STATIC_LIBS TRUE)
# FindOpenSSL only makes its imported targets for archives that already exist
if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto STATIC IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION             "${OPENSSL_LIB_CRYPTO}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_BUILD_DIR}/include")
endif()
if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL STATIC IMPORTED)
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION             "${OPENSSL_LIB_SSL}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_BUILD_DIR}/include"
        INTERFACE_LINK_LIBRARIES      OpenSSL::Crypto)
endif()
set(HAVE_AWSLC 0)
set(HAVE_BORINGSSL 0)
set(HAVE_LIBRESSL 0)
set(HAVE_SSL_SET0_WBIO 1)
set(HAVE_DES_ECB_ENCRYPT 1)

# nghttp2: the target above, found without probing
set(NGHTTP2_INCLUDE_DIR "${EXTERN_DIR}/nghttp2/lib/includes" CACHE PATH   "" FORCE)
set(NGHTTP2_LIBRARY     nghttp2_rp                         CACHE STRING "" FORCE)

# cache-style curl settings (curl sets these with set(CACHE), which would
# drop a plain variable)
set(CURL_ZLIB   ON   CACHE STRING "" FORCE)
set(CURL_BROTLI OFF  CACHE STRING "" FORCE)
set(CURL_ZSTD   OFF  CACHE STRING "" FORCE)
set(CURL_CA_PATH none CACHE STRING "" FORCE)   # bundle only, as before

# option()-style settings, scoped to curl's directory by the function
function(rp_add_curl)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_ENABLE_EXPORT_TARGET OFF)
    set(CURL_USE_PKGCONFIG OFF)
    set(CURL_USE_CMAKECONFIG OFF)
    set(PICKY_COMPILER OFF)
    set(CURL_USE_OPENSSL ON)
    set(USE_NGHTTP2 ON)
    set(CURL_ENABLE_NTLM ON)
    set(CURL_DISABLE_LDAP ON)
    set(CURL_DISABLE_LDAPS ON)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_USE_LIBSSH OFF)
    set(CURL_USE_GSSAPI OFF)
    set(USE_LIBIDN2 OFF)
    add_subdirectory(${EXTERN_DIR}/curl)
endfunction()
rp_add_curl()
add_dependencies(libcurl_static openssl)

# rampart-curl links libcurl.a by path (OpenSSL comes from rampart-crypto.so),
# so it names libcurl's other dependencies itself
set(RP_CURL_SYSLIBS "")
if(APPLE)   # curl uses these for IPv6 interface/proxy lookups on macOS
    list(APPEND RP_CURL_SYSLIBS "-framework SystemConfiguration"
                                "-framework CoreFoundation" "-framework CoreServices")
endif()
