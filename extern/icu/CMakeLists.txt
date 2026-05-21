# Vendored ICU4C 76.1 — provides Intl runtime for rampart-intl.so.
# ICU has its own autoconf+make build system; wrap it via ExternalProject_Add.
# Built as static libs with data baked in so rampart-intl.so is self-contained.

include(ExternalProject)

set(ICU_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/icu-build)
set(ICU_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)
set(ICU_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/icu-install)

find_program(MAKE_EXE NAMES gmake make)

# Pick the right runConfigureICU platform name.
if(APPLE)
    set(ICU_PLATFORM "MacOSX")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ICU_PLATFORM "Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(ICU_PLATFORM "FreeBSD")
else()
    set(ICU_PLATFORM "Linux")  # best-effort default
endif()

# Determine parallelism for ICU's make step. CMake's --parallel doesn't pass
# through to ExternalProject build commands automatically; pass -jN explicitly.
include(ProcessorCount)
ProcessorCount(_ICU_NPROC)
if(_ICU_NPROC EQUAL 0)
    set(_ICU_NPROC 2)
endif()

ExternalProject_Add(icu4c
    PREFIX            ${ICU_PREFIX_DIR}
    SOURCE_DIR        ${ICU_SOURCE_DIR}
    BUILD_IN_SOURCE   0
    BINARY_DIR        ${ICU_PREFIX_DIR}/build
    INSTALL_DIR       ${ICU_INSTALL_DIR}
    # runConfigureICU sets platform-specific compiler defaults, then forwards
    # the remaining flags to configure. Static-only build, no tests/samples/
    # extras/layout, data baked into libicudata as a static archive.
    # runConfigureICU takes CFLAGS/CXXFLAGS from the environment, not
     # positional args (it word-splits "$@" so quoted multi-value vars
     # get mangled). Pass via cmake -E env.
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                        "CFLAGS=-fPIC -fvisibility=hidden"
                        "CXXFLAGS=-fPIC -fvisibility=hidden"
                        ${ICU_SOURCE_DIR}/runConfigureICU ${ICU_PLATFORM}
                        --prefix=${ICU_INSTALL_DIR}
                        --enable-static
                        --disable-shared
                        --disable-tests
                        --disable-samples
                        --disable-extras
                        --disable-layout
                        --disable-layoutex
                        --disable-icuio
                        --disable-dyload
                        --with-data-packaging=static
    BUILD_COMMAND     ${MAKE_EXE} -j${_ICU_NPROC}
    INSTALL_COMMAND   ${MAKE_EXE} install
    LOG_DOWNLOAD      0
    LOG_CONFIGURE     1
    LOG_BUILD         1
    LOG_INSTALL       1
    # When ICU's bundled headers/data update, rerun configure+build.
    BUILD_BYPRODUCTS
        ${ICU_INSTALL_DIR}/lib/libicui18n.a
        ${ICU_INSTALL_DIR}/lib/libicuuc.a
        ${ICU_INSTALL_DIR}/lib/libicudata.a
)

# Expose the install paths to the parent scope so rampart-intl can link.
set(ICU_INCLUDE_DIR ${ICU_INSTALL_DIR}/include CACHE INTERNAL "")
set(ICU_LIB_DIR     ${ICU_INSTALL_DIR}/lib     CACHE INTERNAL "")

# Static-lib targets the parent CMakeLists.txt can depend on.
add_library(icu_i18n  STATIC IMPORTED GLOBAL)
add_library(icu_uc    STATIC IMPORTED GLOBAL)
add_library(icu_data  STATIC IMPORTED GLOBAL)
set_target_properties(icu_i18n  PROPERTIES IMPORTED_LOCATION ${ICU_LIB_DIR}/libicui18n.a)
set_target_properties(icu_uc    PROPERTIES IMPORTED_LOCATION ${ICU_LIB_DIR}/libicuuc.a)
set_target_properties(icu_data  PROPERTIES IMPORTED_LOCATION ${ICU_LIB_DIR}/libicudata.a)
add_dependencies(icu_i18n icu4c)
add_dependencies(icu_uc   icu4c)
add_dependencies(icu_data icu4c)
