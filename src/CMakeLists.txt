set(CMAKE_BUILD_TYPE Debug)
include(CheckCSourceCompiles)

# for robotstxt on macos
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)

# Install prefix is set in top-level CMakeLists.txt
message("install = ${CMAKE_INSTALL_PREFIX}")

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

if(WIN32 AND MINGW)
    # sys/queue.h compat from libevhtp_ws
    include_directories(${CMAKE_BINARY_DIR}/extern/libevhtp_ws/compat)
endif()

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
if(CYGWIN)
    execute_process(
        COMMAND cygpath -m "${CMAKE_C_COMPILER}"
        OUTPUT_VARIABLE BUILD_CC_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(BUILD_CC_PATH "${CMAKE_C_COMPILER}")
endif()
FILE (APPEND ${CMAKE_BINARY_DIR}/include/timestamp.cmake "FILE(APPEND ${CMAKE_BINARY_DIR}/include/rampart_timestamp.h \"#define RAMPART_BUILD_CC \\\"${BUILD_CC_PATH}\\\"\\n\\n\")\n")
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
    ${PROJECT_SOURCE_DIR}/extern/SimSIMD/c/lib.c
    duktape/globals/rampart-vector.c
    duktape/globals/vector-distance.c
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

# I'm guessing given simsimd's nature, many of these warnings are unavoidable.  Let's silence them:
set(SIMSIMD_WARN "-Wno-unused-function -Wno-unknown-pragmas -Wno-unused-variable -Wno-unused-but-set-variable")

string(ASCII 27 ESC)
set(GREEN "${ESC}[32m")
set(RESET "${ESC}[0m")

set(SIMSIMD_FLAGS "-DSIMSIMD_DYNAMIC_DISPATCH=1")
set(SIMSIMD_ARCH_FLAGS "")
set(SIMSIMD_DISABLE_TARGETS "")


if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS "4.9")
    message(STATUS "GCC ${CMAKE_C_COMPILER_VERSION} too old, disabling all SIMD targets")
    set(SIMSIMD_DISABLE_TARGETS
      "-DSIMSIMD_TARGET_HASWELL=0 -DSIMSIMD_TARGET_SKYLAKE=0 -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
    set(SIMSIMD_FLAGS "${SIMSIMD_FLAGS} ${SIMSIMD_DISABLE_TARGETS}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(i686|i386)")
  message(STATUS "32 bit system, disabling all SIMD targets")
  set(SIMSIMD_DISABLE_TARGETS
    "-DSIMSIMD_TARGET_HASWELL=0 -DSIMSIMD_TARGET_SKYLAKE=0 -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
  set(SIMSIMD_FLAGS "${SIMSIMD_FLAGS} ${SIMSIMD_DISABLE_TARGETS}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i686|i386)")
    message(WARNING "Testing x86_64 instruction set support...")

    # Test Haswell (AVX2, FMA, F16C)
    set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma -mf16c")
    check_c_source_compiles("
        #include <immintrin.h>
        int main() {
            __m256 a = _mm256_setzero_ps();
            __m256d b = _mm256_setzero_pd();
            __m128 c = _mm_setzero_ps();
            __m256d d = _mm256_cvtps_pd(c);
            __m256 e = _mm256_fmadd_ps(a, a, a);
            __m128i f = _mm_setzero_si128();
            __m256 g = _mm256_cvtph_ps(f);
            __m128i h = _mm_lddqu_si128((__m128i const *)&f);
          __m256 result = _mm256_cvtph_ps(h);
            return 0;
        }
    " HAVE_HASWELL)

    if(HAVE_HASWELL)
        message("-- ${GREEN}Haswell (AVX2)${RESET}")
        set(SIMSIMD_ARCH_FLAGS "${SIMSIMD_ARCH_FLAGS} -mavx2 -mfma -mf16c")
    else()
        message(WARNING "Haswell - disabling all SIMD")
        set(SIMSIMD_DISABLE_TARGETS "-DSIMSIMD_TARGET_HASWELL=0 -DSIMSIMD_TARGET_SKYLAKE=0 -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
    endif()

    if(HAVE_HASWELL AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        # Test Skylake (AVX512F, AVX512DQ, AVX512BW, AVX512VL)
        set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma -mf16c -mavx512f -mavx512dq -mavx512bw -mavx512vl")
        check_c_source_compiles("
            #include <immintrin.h>
            int main() {
                __m512 a = _mm512_setzero_ps();
                __m512 b = _mm512_fmadd_ps(a, a, a);

                // Test _mm512_loadu_epi8 which is used in SIMSIMD binary.h
                unsigned char data[64] = {0};
                __m512i vec = _mm512_loadu_epi8(data);

                // Test other common operations
                __m512i ivec = _mm512_setzero_si512();
                __mmask64 mask = _mm512_cmpneq_epi8_mask(vec, ivec);

                return 0;
            }
        " HAVE_SKYLAKE)

        if(NOT HAVE_SKYLAKE)
            message(WARNING "Skylake (AVX512) - disabling Skylake/Ice/Genoa/Sapphire/Turin")
            set(SIMSIMD_DISABLE_TARGETS "${SIMSIMD_DISABLE_TARGETS} -DSIMSIMD_TARGET_SKYLAKE=0 -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
        else()
            set(SIMSIMD_ARCH_FLAGS "${SIMSIMD_ARCH_FLAGS} -mavx512f -mavx512dq -mavx512bw -mavx512vl")
            message("-- ${GREEN}Skylake (AVX512)${RESET}")
        endif()
    else()
        set(SIMSIMD_DISABLE_TARGETS "-DSIMSIMD_TARGET_SKYLAKE=0 -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
    endif()

    if(HAVE_HASWELL AND HAVE_SKYLAKE)
        # Test Ice Lake (AVX512VNNI, AVX512VBMI2, AVX512BITALG)
        set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma -mf16c -mavx512f -mavx512dq -mavx512bw -mavx512vl -mavx512vnni -mavx512vbmi2 -mavx512bitalg -mavx512vp2in")
        check_c_source_compiles("
            #include <immintrin.h>
            int main() {
                __m512i a = _mm512_setzero_si512();
                __m512i b = _mm512_dpbusd_epi32(a, a, a);
                return 0;
            }
        " HAVE_ICE)

        if(NOT HAVE_ICE)
            message(WARNING "Ice Lake - disabling Ice/Genoa/Sapphire/Turin")
            set(SIMSIMD_DISABLE_TARGETS "${SIMSIMD_DISABLE_TARGETS} -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
        else()
            set(SIMSIMD_ARCH_FLAGS "${SIMSIMD_ARCH_FLAGS} -mavx512vnni -mavx512vbmi2 -mavx512bitalg")
            message("-- ${GREEN}Ice Lake${RESET}")
        endif()
    endif()

    if(HAVE_HASWELL AND HAVE_SKYLAKE AND HAVE_ICE)
        # Test Sapphire Rapids (VP2INTERSECT)
        set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma -mf16c -mavx512f -mavx512dq -mavx512bw -mavx512vl -mavx512vnni -mavx512vbmi2 -mavx512bitalg -mavx512vp2intersect -mavx512fp16")
        set(CMAKE_REQUIRED_FLAGS
             "-mavx2 -mfma -mf16c -mavx512f -mavx512dq -mavx512bw -mavx512vl -mavx512vnni -mavx512vbmi2 -mavx512bitalg -mavx512vp2intersect -mavx512fp16")
        check_c_source_compiles("
            #include <immintrin.h>
            int main() {
                // VP2INTERSECT
                __m512i a = _mm512_setzero_si512();
                __mmask16 m1, m2;
                _mm512_2intersect_epi32(a, a, &m1, &m2);

                // FP16
                __m512h h = _mm512_setzero_ph();
                __m512h h2 = _mm512_fmadd_ph(h, h, h);

                return 0;
            }
        " HAVE_SAPPHIRE)

        if(NOT HAVE_SAPPHIRE)
            message(WARNING "Sapphire Rapids - disabling Sapphire")
            set(SIMSIMD_DISABLE_TARGETS "${SIMSIMD_DISABLE_TARGETS} -DSIMSIMD_TARGET_SAPPHIRE=0")
        else()
            set(SIMSIMD_ARCH_FLAGS "${SIMSIMD_ARCH_FLAGS} -mavx512vp2intersect -mavx512fp16")
            message("-- ${GREEN}Sapphire Rapids${RESET}")
        endif()

        # Test Turin/Genoa (AVX512BF16)
        set(CMAKE_REQUIRED_FLAGS "-mavx512bf16")
        check_c_source_compiles("
            #include <immintrin.h>
            int main() {
                __m512bh a = (__m512bh)_mm512_setzero_si512();
                __m512 b = _mm512_dpbf16_ps(_mm512_setzero_ps(), a, a);
                return 0;
            }
        " HAVE_BF16)

        if(NOT HAVE_BF16)
            message(WARNING "BF16 - disabling Turin/Genoa")
            set(SIMSIMD_DISABLE_TARGETS "${SIMSIMD_DISABLE_TARGETS} -DSIMSIMD_TARGET_TURIN=0 -DSIMSIMD_TARGET_GENOA=0")
        else()
            set(SIMSIMD_ARCH_FLAGS "${SIMSIMD_ARCH_FLAGS} -mavx512bf16")
            message("-- ${GREEN}BF16 (Turin/Genoa)${RESET}")
        endif()
    endif()
endif()

set(SIMSIMD_FLAGS "${SIMSIMD_FLAGS} ${SIMSIMD_DISABLE_TARGETS}")
message("-- ${GREEN}Final flags: ${SIMSIMD_FLAGS}${RESET}")
message("-- ${GREEN}Arch flags: ${SIMSIMD_ARCH_FLAGS}${RESET}")

set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/extern/SimSIMD/c/lib.c
  PROPERTIES
    COMPILE_FLAGS "-O3 ${SIMSIMD_WARN} ${SIMSIMD_FLAGS} ${SIMSIMD_ARCH_FLAGS}"
)
set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/src/duktape/globals/vector-distance.c
  PROPERTIES
    COMPILE_FLAGS "-O3 ${SIMSIMD_WARN}"
)

set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/src/duktape/globals/rampart-vector.c
  PROPERTIES
    COMPILE_FLAGS "-O3 ${SIMSIMD_WARN}"
)

set_source_files_properties(
  ${PROJECT_SOURCE_DIR}/extern/SimSIMD/c/lib.c
  PROPERTIES
    COMPILE_FLAGS "-O3 ${SIMSIMD_WARN} ${SIMSIMD_FLAGS} ${SIMSIMD_ARCH_FLAGS}"
)

set_source_files_properties(
  duktape/globals/vector-distance.c
  PROPERTIES
    COMPILE_FLAGS "-O3 ${SIMSIMD_WARN}"
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
    ${PROJECT_SOURCE_DIR}/extern/SimSIMD/include
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

if(CYGWIN)
    # Cygwin/MSYS pthreads lack robust mutex support
    set(LMDB_XCFLAGS "-fPIC -DMDB_USE_ROBUST=0")
else()
    set(LMDB_XCFLAGS "-fPIC")
endif()

add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/extern/lmdb/liblmdb.a
    COMMAND ${MAKE} "XCFLAGS=${LMDB_XCFLAGS}"
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
if(CYGWIN)
  # Avoid circular dependency: rampart -> texislockd-cp -> rampart-sql -> rampart
  # texislockd-cp only needs texisapi built (to produce the texislockd binary)
  add_dependencies(texislockd-cp texisapi)
else()
  add_dependencies(texislockd-cp rampart-sql)
endif()

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
#    OUTPUT ${PROJECT_BINARY_DIR}/extern/libdeflate/libdeflate.a
#    COMMAND ${MAKE}
#    COMMAND mkdir -p ${PROJECT_BINARY_DIR}/extern/libdeflate
#    COMMAND find './' -type f  -perm 0755 -regex '.*[^\.b][^sa][^ht]' -exec mv {} "${PROJECT_BINARY_DIR}/extern/libdeflate" "\;"
#    COMMAND mv libdeflate.a ${PROJECT_BINARY_DIR}/extern/libdeflate/
#    COMMAND cp libdeflate.h ${PROJECT_BINARY_DIR}/extern/libdeflate/
#    COMMAND ${MAKE} clean
#    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/extern/libdeflate/
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
if(CYGWIN)
    target_link_libraries(rampart-net dl)
endif()

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

if(CYGWIN)
  # MSYS platform name (e.g. msys_nt-10.0-17763) contains periods that break
  # Python's _sysconfigdata module import. Set MACHDEP=msys for a clean name.
  # Pass -std=gnu11 via CFLAGS to configure so Python uses it throughout (GCC 15 C23 compat).
  # Disable --enable-optimizations: PGO requires _testembed which fails to link on Cygwin.
  # Use --disable-shared: Cygwin's DLL model causes __imp_ symbol errors in _testembed and
  # shared extension modules. Static build embeds all modules into libpython3.11.a.
  set(PYTHON_EXTRA_ENV "MACHDEP=msys CFLAGS=-std=gnu11")
  set(PYTHON_MAKE_FLAGS "")
  set(PYTHON_CONFIGURE_OPTS "--disable-shared --prefix=\"${PROJECT_BINARY_DIR}/extern/${PYTHON_DIR_NAME}/build\"")
else()
  set(PYTHON_EXTRA_ENV "")
  set(PYTHON_MAKE_FLAGS "CFLAGS=${CMAKE_C_FLAGS}")
  set(PYTHON_CONFIGURE_OPTS "--enable-optimizations --prefix=\"${PROJECT_BINARY_DIR}/extern/${PYTHON_DIR_NAME}/build\"")
endif()

if(CYGWIN)
  # Use MSYS2's bash explicitly — Git for Windows' bash has an incompatible
  # msys-2.0.dll that prevents configure test programs from executing.
  set(BASH_CMD /usr/bin/bash)
else()
  set(BASH_CMD bash)
endif()

add_custom_command(
    OUTPUT ${PYTHON_BIN_DIR}/Makefile
    DEPENDS rampart-crypto
    COMMAND ${BASH_CMD} -c "if [ ! -e ${PYTHON_BIN_DIR}/Makefile ]; then ${PYTHON_EXTRA_ENV} RAMPART_SSL_DIR=${PROJECT_BINARY_DIR}/extern/openssl PKG_CONFIG_PATH=\"${PYPKG}\" LDFLAGS=\"${PYLDFLAGS}\" CPPFLAGS=\"${PYFLAGS}\" TCLTK_LIBS=\"-ltk -ltcl\" ./configure ${PYTHON_CONFIGURE_OPTS};fi"
    WORKING_DIRECTORY ${PYTHON_BIN_DIR}
    VERBATIM
)

if(CYGWIN)
# On Cygwin, Python's build_all target includes _testembed and shared extension
# modules that fail to link (no shared libpython). Use a helper script to build
# only what we need: the static library, the python binary, and install.
add_custom_command(
    DEPENDS ${PYTHON_BIN_DIR}/Makefile
    OUTPUT ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
    COMMAND ${BASH_CMD} ${PROJECT_SOURCE_DIR}/src/build_python_cygwin.sh ${PYTHON_MINOR} ${CMAKE_MAKE_PROGRAM}
    WORKING_DIRECTORY ${PYTHON_BIN_DIR}
)
else()
add_custom_command(
    DEPENDS ${PYTHON_BIN_DIR}/Makefile
    OUTPUT ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
    COMMAND ${CMAKE_MAKE_PROGRAM} ${PYTHON_MAKE_FLAGS}
    COMMAND ${CMAKE_MAKE_PROGRAM} ${PYTHON_MAKE_FLAGS} install
    WORKING_DIRECTORY ${PYTHON_BIN_DIR}
)
endif()

add_custom_target(libpython
    DEPENDS ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
    COMMAND bash -c "rm -rf ${PROJECT_BINARY_DIR}/src/python3-lib"
    COMMAND bash -c "ln -s ${PYTHON_BIN_DIR}/build/lib/python3.${PYTHON_MINOR} ${PROJECT_BINARY_DIR}/src/python3-lib"
)

add_library(rampart-python SHARED
    duktape/modules/rampart-python.c
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

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

elseif(CYGWIN)
        # On Cygwin, Python extension modules are statically linked into
        # libpython3.x.a (shared modules can't be built without shared libpython).
        # We must link the supporting libraries that those static modules need.
        target_link_libraries(rampart-python PRIVATE
                ${PYTHON_BIN_DIR}/libpython3.${PYTHON_MINOR}.a
                ${PYTHON_BIN_DIR}/Modules/_decimal/libmpdec/libmpdec.a
                ${PYTHON_BIN_DIR}/Modules/expat/libexpat.a
                ${OPENSSL_TARGET_DIR}/ssl/libssl.a
                ${OPENSSL_TARGET_DIR}/crypto/libcrypto.a
                pthread dl util m z sqlite3 intl
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
#    /path/to/library/file.a
#)
#target_include_directories(example_name PRIVATE
#    /path/to/include1
#    /path/to/include2
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
elseif(CYGWIN)
  # On Cygwin, shared modules must link against the rampart executable's
  # import library to resolve duktape and other symbols at link time.
  # cmake doesn't register the import library as a build output for executables,
  # so we link against the file directly and add explicit dependencies.
  set(RAMPART_IMPLIB "${CMAKE_CURRENT_BINARY_DIR}/librampart.dll.a")
  # Targets using plain signature (match existing target_link_libraries form)
  target_link_libraries(rampart-almanac ${RAMPART_IMPLIB})
  target_link_libraries(rampart-cmark ${RAMPART_IMPLIB})
  target_link_libraries(rampart-html ${RAMPART_IMPLIB})
  target_link_libraries(rampart-lmdb ${RAMPART_IMPLIB})
  target_link_libraries(rampart-net ${RAMPART_IMPLIB})
  target_link_libraries(rampart-redis ${RAMPART_IMPLIB})
  target_link_libraries(rampart-robots ${RAMPART_IMPLIB})
  target_link_libraries(rampart-server ${RAMPART_IMPLIB})
  # Targets using keyword signature (match existing target_link_libraries form)
  target_link_libraries(rampart-crypto PRIVATE ${RAMPART_IMPLIB})
  target_link_libraries(rampart-curl PRIVATE ${RAMPART_IMPLIB})
  target_link_libraries(rampart-python PRIVATE ${RAMPART_IMPLIB})
  target_link_libraries(rampart-sql PRIVATE ${RAMPART_IMPLIB})
  # Ensure modules are built after rampart (which generates the import library)
  foreach(_mod rampart-almanac rampart-cmark rampart-html rampart-lmdb
               rampart-net rampart-redis rampart-robots rampart-server
               rampart-crypto rampart-curl rampart-python rampart-sql)
    add_dependencies(${_mod} rampart)
  endforeach()
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

if(CYGWIN)
    install(CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E copy \
        \"${PROJECT_SOURCE_DIR}/src/run_tests.ps1\" \
        \"${CMAKE_INSTALL_PREFIX}/\"   \
        )"
    )
    install(CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E copy \
        \"${PROJECT_SOURCE_DIR}/src/run_tests.bat\" \
        \"${CMAKE_INSTALL_PREFIX}/\"   \
        )"
    )
    install(CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E copy \
        \"${PROJECT_SOURCE_DIR}/src/install.bat\" \
        \"${CMAKE_INSTALL_PREFIX}/\"   \
        )"
    )
    install(CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E copy \
        \"${PROJECT_SOURCE_DIR}/src/install.ps1\" \
        \"${CMAKE_INSTALL_PREFIX}/\"   \
        )"
    )
endif()

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
    \"${PROJECT_SOURCE_DIR}/src/include/rp_string.h\" \
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
    COMMAND bash -c \"chmod 777 '${CMAKE_INSTALL_PREFIX}/test'\"
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
    '${CMAKE_INSTALL_PREFIX}/modules/python' \"\
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
    '${PYTHON_BIN_DIR}/build/bin' \
    '${PYTHON_BIN_DIR}/build/lib' \
    '${CMAKE_INSTALL_PREFIX}/modules/python/' \"\
    )"
)

install(
    CODE "execute_process( COMMAND bash \"${CMAKE_INSTALL_PREFIX}/modules/python/rebase-python.sh\"  \
    WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/modules/python\")"
)

install(CODE "execute_process( \
    COMMAND bash -c \"rm -rf \
    '${CMAKE_INSTALL_PREFIX}/modules/python3-lib' \
    '${CMAKE_INSTALL_PREFIX}/bin/python3r' \
    '${CMAKE_INSTALL_PREFIX}/bin/pip3r' \"\
    )"
)

if(CYGWIN)
    # Use copies instead of symlinks on Cygwin (symlinks may not work on vanilla Windows)
    install(CODE "execute_process( \
        COMMAND bash -c \"cp -a \
        '${CMAKE_INSTALL_PREFIX}/modules/python/lib/python3.${PYTHON_MINOR}' \
        '${CMAKE_INSTALL_PREFIX}/modules/python3-lib' \"\
        )"
    )
    install(CODE "execute_process( \
        COMMAND bash -c \"cp \
        '${CMAKE_INSTALL_PREFIX}/modules/python/bin/python3' \
        '${CMAKE_INSTALL_PREFIX}/bin/python3r' \"\
        )"
    )
    install(CODE "execute_process( \
        COMMAND bash -c \"cp \
        '${CMAKE_INSTALL_PREFIX}/modules/python/bin/pip3' \
        '${CMAKE_INSTALL_PREFIX}/bin/pip3r' \"\
        )"
    )
else()
    install(CODE "execute_process( \
        COMMAND bash -c \"ln -s \
        python/lib/python3.${PYTHON_MINOR} \
        '${CMAKE_INSTALL_PREFIX}/modules/python3-lib' \"\
        )"
    )
    install(CODE "execute_process( \
        COMMAND bash -c \"ln -s \
        ../modules/python/bin/python3 \
        '${CMAKE_INSTALL_PREFIX}/bin/python3r' \"\
        )"
    )
    install(CODE "execute_process( \
        COMMAND bash -c \"ln -s \
        ../modules/python/bin/pip3 \
        '${CMAKE_INSTALL_PREFIX}/bin/pip3r' \"\
        )"
    )
endif()


install(CODE "execute_process( \
    COMMAND bash -c \"chmod 755 '${CMAKE_INSTALL_PREFIX}/modules/python/bin/python3.${PYTHON_MINOR}' \"\
    )"
)
install(CODE "execute_process( \
    COMMAND bash -c \"chmod 755 '${CMAKE_INSTALL_PREFIX}/modules/python/bin/pip3' \"\
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
install(CODE "execute_process( COMMAND bash -c \"cp -a '${PROJECT_SOURCE_DIR}/derivations'/* '${CMAKE_INSTALL_PREFIX}/derivations/'\")" )

if (CMAKE_UID EQUAL 0)
    install(CODE "execute_process ( \
        COMMAND bash -c \"chown -R nobody '${CMAKE_INSTALL_PREFIX}/web_server'\"
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

# Bundle MSYS2 runtime DLLs for standalone Windows operation
if(CYGWIN)
    set(MSYS_DLLS
        msys-2.0.dll
        msys-gcc_s-seh-1.dll
        msys-stdc++-6.dll
        msys-z.dll
        msys-intl-8.dll
        msys-iconv-2.dll
        msys-sqlite3-0.dll
    )
    foreach(DLL ${MSYS_DLLS})
        if(EXISTS "/usr/bin/${DLL}")
            install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E copy \"/usr/bin/${DLL}\" \"${CMAKE_INSTALL_PREFIX}/bin/${DLL}\")")
        else()
            message(WARNING "MSYS2 DLL not found: /usr/bin/${DLL}")
        endif()
    endforeach()

    # The relocated msys-2.0.dll computes the Cygwin root as two levels
    # up from its location (<prefix>/bin/msys-2.0.dll -> <prefix>/..).
    # POSIX semaphores and shared memory require /dev/shm to exist
    # relative to that root.  Create it so rampart works standalone.
    cmake_path(GET CMAKE_INSTALL_PREFIX PARENT_PATH _MSYS_ROOT)
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory \"${_MSYS_ROOT}/dev/shm\")")

    # Bundle MSYS2 CA certificate bundle so HTTPS works standalone.
    # The find_bundle() fallback checks <installdir>/etc/ca-bundle.crt.
    if(EXISTS "/usr/ssl/certs/ca-bundle.crt")
        install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory \"${CMAKE_INSTALL_PREFIX}/etc\")")
        install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E copy \"/usr/ssl/certs/ca-bundle.crt\" \"${CMAKE_INSTALL_PREFIX}/etc/ca-bundle.crt\")")
    else()
        message(WARNING "MSYS2 CA bundle not found at /usr/ssl/certs/ca-bundle.crt")
    endif()
endif()
