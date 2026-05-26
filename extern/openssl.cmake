# OpenSSL build wiring — vendored 4.0.0 via the project's own
# ./Configure + make.  Follows the same shape as the Python build in
# src/CMakeLists.txt: copy the source tree to ${BUILD}/extern/openssl,
# Configure there, run make.  Linking and include paths stay
# unchanged from the old custom-CMakeLists layout so the rest of the
# tree doesn't need to follow.
#
# Produces (all under ${OPENSSL_BUILD_DIR}):
#   libssl.a, libcrypto.a               — at the build root
#   ssl/libssl.a, crypto/libcrypto.a    — compat symlinks for callers
#                                         that still reference the
#                                         old subdir layout
#                                         (libevent, libevhtp_ws,
#                                         curl, Python's configure)
#   include/openssl/*.h                 — populated by ./Configure
#
# Exposes target  `openssl`  (add_dependencies(rampart-* openssl) etc.)

set(OPENSSL_SRC_DIR     ${EXTERN_DIR}/openssl)
set(OPENSSL_BUILD_DIR   ${CMAKE_BINARY_DIR}/extern/openssl)
set(OPENSSL_LIB_SSL     ${OPENSSL_BUILD_DIR}/libssl.a)
set(OPENSSL_LIB_CRYPTO  ${OPENSSL_BUILD_DIR}/libcrypto.a)

# Pick OpenSSL Configure target.  `./config` autodetects on every
# supported host; we name targets explicitly only where autodetect is
# known to misfire on older systems.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armhf|armv6|armv7|armv7l)$")
        set(OPENSSL_TARGET "linux-armv4")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(OPENSSL_TARGET "linux-aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(OPENSSL_TARGET "linux-x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
        set(OPENSSL_TARGET "linux-x86")
    else()
        set(OPENSSL_TARGET "")  # let ./config autodetect
    endif()
elseif(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(OPENSSL_TARGET "darwin64-arm64-cc")
    else()
        set(OPENSSL_TARGET "darwin64-x86_64-cc")
    endif()
elseif(CYGWIN)
    set(OPENSSL_TARGET "Cygwin-x86_64")
else()
    set(OPENSSL_TARGET "")
endif()

# Configure flags:
#   no-shared   — static-only build (we link libssl.a / libcrypto.a)
#   no-module   — bake providers (default/legacy/fips) into libcrypto.a
#                 instead of building them as separate `.so` DSO modules.
#                 Required for a truly static build: otherwise
#                 OSSL_PROVIDER_load("legacy") looks for legacy.so on
#                 disk and md4 / mdc2 / rmd160 stop working.
#   no-tests    — skip the test suite (also avoids the two 32-bit-only
#                 OOM-allocator test failures that aren't library bugs)
#   no-docs     — skip manpage generation
#   no-apps     — skip the openssl(1) CLI; we don't ship it
#   -fPIC       — required: the static libs get linked into
#                 rampart-crypto.so / rampart-net.so / rampart-server.so
#   --prefix    — only matters if `make install` runs; harmless
set(_OPENSSL_CONFIGURE_ARGS
    no-shared no-module no-tests no-docs no-apps -fPIC -g
    --prefix=${OPENSSL_BUILD_DIR}
)

# On Apple, OpenSSL's Configure ignores the parent build's
# -mmacosx-version-min flag, so its object files end up tagged with
# the host SDK version (e.g. 15.0) and produce loud linker warnings
# when linked into a binary targeting an older deployment target.
# Pass the deployment-target flag directly to Configure as a compiler
# flag so OpenSSL's compile commands carry it.
if(APPLE)
    list(APPEND _OPENSSL_CONFIGURE_ARGS -mmacosx-version-min=11.0)
endif()

include(ProcessorCount)
ProcessorCount(_NPROC)
if(_NPROC EQUAL 0)
    set(_NPROC 2)
endif()

# Use $(MAKE) instead of ${CMAKE_MAKE_PROGRAM} for the build step.
# CMake passes $(MAKE) through unescaped (it only expands ${}); the
# generated Makefile then contains a literal $(MAKE) recipe line.
# gmake gives $(MAKE) implicit jobserver-passthrough semantics, so the
# parent make's -jN is inherited.  -j${_NPROC} is kept as a fallback
# for standalone invocations (cmake --build . --target openssl) where
# there's no parent jobserver; when the jobserver IS present, gmake
# uses it and ignores -j${_NPROC}.

# Step 1: copy source tree to the build dir (once).
add_custom_command(
    OUTPUT  ${OPENSSL_BUILD_DIR}/VERSION.dat
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/extern
    COMMAND cp -a ${OPENSSL_SRC_DIR} ${CMAKE_BINARY_DIR}/extern/
    COMMENT "Copying OpenSSL source to build dir"
)

# Step 2: ./Configure in the build copy.
if(OPENSSL_TARGET)
    set(_CFG_CMD ./Configure ${OPENSSL_TARGET})
else()
    set(_CFG_CMD ./config)
endif()
add_custom_command(
    DEPENDS ${OPENSSL_BUILD_DIR}/VERSION.dat
    OUTPUT  ${OPENSSL_BUILD_DIR}/Makefile
    COMMAND ${_CFG_CMD} ${_OPENSSL_CONFIGURE_ARGS}
    WORKING_DIRECTORY ${OPENSSL_BUILD_DIR}
    COMMENT "Configuring OpenSSL (${OPENSSL_TARGET})"
)

# Step 3: build the libs.  Also drop compat symlinks into ssl/ and
# crypto/ subdirs so callers that hard-coded the old subdir paths
# (libevent, libevhtp_ws, curl, Python's configure) keep working.
add_custom_command(
    DEPENDS ${OPENSSL_BUILD_DIR}/Makefile
    OUTPUT  ${OPENSSL_LIB_SSL} ${OPENSSL_LIB_CRYPTO}
            ${OPENSSL_BUILD_DIR}/ssl/libssl.a
            ${OPENSSL_BUILD_DIR}/crypto/libcrypto.a
    COMMAND $(MAKE) -j${_NPROC} build_libs
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OPENSSL_BUILD_DIR}/ssl
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OPENSSL_BUILD_DIR}/crypto
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libssl.a    ${OPENSSL_BUILD_DIR}/ssl/libssl.a
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libcrypto.a ${OPENSSL_BUILD_DIR}/crypto/libcrypto.a
    WORKING_DIRECTORY ${OPENSSL_BUILD_DIR}
    COMMENT "Building OpenSSL libraries"
)

# Step 4: top-level target other targets can depend on.
add_custom_target(openssl
    DEPENDS ${OPENSSL_LIB_SSL} ${OPENSSL_LIB_CRYPTO}
            ${OPENSSL_BUILD_DIR}/ssl/libssl.a
            ${OPENSSL_BUILD_DIR}/crypto/libcrypto.a
)

# IMPORTED library targets `ssl` and `crypto`.  The previous custom-
# CMakeLists OpenSSL build exposed these as ordinary add_library
# targets; libevent's CMakeLists references them by name
# (`target_link_libraries(event_openssl INTERFACE ssl crypto)`).  We
# preserve those names as IMPORTED static libs that depend on the
# `openssl` build target above, so nothing in libevent / curl /
# libevhtp_ws needs to change.
# Make sure the include dir exists at cmake-configure time so the
# INTERFACE_INCLUDE_DIRECTORIES below doesn't trip CMake's
# "doesn't-exist" check.  The contents get populated by Configure
# at build time.
file(MAKE_DIRECTORY ${OPENSSL_BUILD_DIR}/include)

add_library(crypto STATIC IMPORTED GLOBAL)
set_target_properties(crypto PROPERTIES
    IMPORTED_LOCATION             ${OPENSSL_LIB_CRYPTO}
    INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_BUILD_DIR}/include)
add_dependencies(crypto openssl)

add_library(ssl STATIC IMPORTED GLOBAL)
set_target_properties(ssl PROPERTIES
    IMPORTED_LOCATION             ${OPENSSL_LIB_SSL}
    INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_BUILD_DIR}/include)
add_dependencies(ssl openssl)
