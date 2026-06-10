## to build a macos universal, run:
#   cmake -DMACOS_UNIVERSAL=1 ../

## to build with -fsanitize=address
#   cmake -DRP_SANITIZE=1 ../
# Run with ASAN_OPTIONS=detect_leaks=0:detect_odr_violation=0 —
# rampart intentionally skips some frees at exit, and the dlopen'd
# modules embed duplicate copies of some src/ globals (e.g.
# rp_runtime_caps_is_init), which the ODR checker would flag.
# Add fast_unwind_on_malloc=false if stack traces are incomplete.
# and if python export PYTHONMALLOC=malloc
# or pymalloc will be used and asan will miss massive amounts of frees

## to use SimSIMD's faster-but-much-less-accurate Quake-style fast invsqrt
## for the SERIAL scalar fallback (cosine/L2/log paths used when no SIMD
## kernel matches the runtime CPU), run:
#   cmake -DRP_SIMSIMD_APPROXIMATE_SCALAR=1 ../
# Default behavior uses libc's exact 1/sqrt for serial fallbacks, which
# costs ~2% on the serial path but keeps cosine/L2 results bit-accurate.

cmake_minimum_required(VERSION 3.13)

project(Rampart VERSION 0.5.0 LANGUAGES C CXX)

# Suppress all warnings on vendored upstream code we don't maintain
# (libevent, jansson, abseil, re2, cre2, usearch, openssl).  Set OFF
# when auditing one of these or after a vendor refresh.
option(SILENCE_VENDORED_WARNINGS "Silence warnings on vendored upstream libraries" ON)

# Texis is legacy C code maintained in-tree by this project.  These are
# specific warning categories audited as benign (analysis 2026-05-05).
# Set OFF to re-surface them if something new breaks.  Anything not in
# the list below will still warn.
option(TEXIS_SILENCE_AUDITED_WARNINGS "Silence the audited texis warnings" ON)
# All audited occurrences are in the legacy C sources, so each flag is
# gated to C compiles ($<COMPILE_LANGUAGE:C>).  The few C++ files in these
# targets (e.g. vecindex_ivfpq.cpp) compile clean without them, and passing
# the list to a C++ compile makes GCC's cc1plus warn about the C-only /
# clang-only spellings (-Wno-pointer-to-int-cast, -Wno-unsequenced, ...).
set(TEXIS_AUDITED_WARNING_FLAGS
    $<$<COMPILE_LANGUAGE:C>:-Wno-unsequenced>                   # 147x: macros mutate shared state across one expr
    $<$<COMPILE_LANGUAGE:C>:-Wno-enum-compare>                  #  39x: intentional cross-enum compares (op codes)
    $<$<COMPILE_LANGUAGE:C>:-Wno-return-type>                   #   8x: unreachable fall-through paths
    $<$<COMPILE_LANGUAGE:C>:-Wno-switch>                        #   6x: enum switch doesn't cover every value
    $<$<COMPILE_LANGUAGE:C>:-Wno-dangling-else>                 #   4x: stylistic if-else nesting
    $<$<COMPILE_LANGUAGE:C>:-Wno-unused-value>                  #   2x: comma-op macro fragments
    $<$<COMPILE_LANGUAGE:C>:-Wno-string-plus-int>               #   2x: intentional pointer-into-string arithmetic
    $<$<COMPILE_LANGUAGE:C>:-Wno-tautological-constant-compare> #   1x: constant-shift always-true
    $<$<COMPILE_LANGUAGE:C>:-Wno-pointer-to-int-cast>           #   1x: pthread_t truncation for logging only
    $<$<COMPILE_LANGUAGE:C>:-Wno-extern-c-compat>               #   1x: empty union size differs C vs C++
)

# Always ship debug info from the build tree; `strip -S` on install removes
# the debug sections, so installed binaries stay small.  Without this,
# vendored cmake-subprojects (curl, libevent, oniguruma, cmark-gfm, ...)
# pick up CMAKE_*_FLAGS_RELEASE which has no -g, leaving valgrind/gdb
# unable to resolve symbols in those libs.
foreach(_lang C CXX)
    foreach(_cfg RELEASE MINSIZEREL)
        if(NOT CMAKE_${_lang}_FLAGS_${_cfg} MATCHES "-g( |$)")
            set(CMAKE_${_lang}_FLAGS_${_cfg} "${CMAKE_${_lang}_FLAGS_${_cfg}} -g"
                CACHE STRING "" FORCE)
        endif()
    endforeach()
endforeach()

# Set install prefix (before any add_subdirectory calls)
# Override the default /usr/local, but respect a user-supplied -DCMAKE_INSTALL_PREFIX=...
if(NOT CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    if(CYGWIN)
        set(CMAKE_INSTALL_PREFIX "/c/Program Files/mcs/rampart" CACHE PATH "" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/usr/local/rampart" CACHE PATH "" FORCE)
    endif()
endif()
message("install = ${CMAKE_INSTALL_PREFIX}")

if(CYGWIN)
  # GCC 15 defaults to C23 which breaks many third-party libraries and cmake
  # configure checks (K&R function declarations, 0 as null pointer, etc.).
  # Force gnu11 for compatibility. Using CMAKE_C_FLAGS ensures this applies
  # to both cmake try_compile checks and actual compilation.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
endif()

# 32-bit ARM (Raspberry Pi etc.): GCC doesn't auto-link libatomic for
# 8-byte (__atomic_*_8) intrinsics that OpenSSL 4.0's threads_pthread.c
# uses for RCU.  Python's _hashlib has the same issue.  Add -latomic to
# every link step so libcrypto.a + Python extensions resolve cleanly.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armhf|armv6|armv7|armv7l)$")
    add_link_options(-latomic)
endif()

#set(CMAKE_BUILD_TYPE Debug)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

    # -Wno-deprecated-non-prototype added in AppleClang 13+
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "13.0")
        add_compile_options(-Wno-deprecated-non-prototype)
    endif()

    #no longer supported - libraries used and python aren't universal
    if (MACOS_UNIVERSAL)

        message("Making MacOs Universal Binary Build")
        SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -mmacosx-version-min=11.0 -arch arm64")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch x86_64 -mmacosx-version-min=11.0 -arch arm64")

    else(MACOS_UNIVERSAL)

        if( CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" )
            SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=11.0 -arch arm64")
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=11.0 -arch arm64")
	else()
            SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch x86_64 -mmacosx-version-min=11.0")
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch x86_64 -mmacosx-version-min=11.0")
        endif( CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" )

    endif (MACOS_UNIVERSAL)

endif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

# -DRP_SANITIZE=1 builds the ENTIRE in-tree CMake build with AddressSanitizer,
# not just src/.  Folding the flags into CMAKE_*_FLAGS here (before any
# add_subdirectory / include(extern.cmake)) makes every native target inherit
# them: src/ and its loadable modules, plus the vendored add_subdirectory deps
# (faiss, texis, oniguruma, curl, libevent, libevhtp_ws, tidy, cmark, robotstxt,
# upa).  Linker flags cover executables, shared libs, and the .so modules.
# Using CMAKE_*_FLAGS (vs add_compile_options) also instruments try_compile
# feature checks and any sub-build that forwards CMAKE_*_FLAGS, and the vendored
# `-w` save/restore in extern.cmake preserves these since they're already in
# CMAKE_C_FLAGS at save time.
#
# NOTE: the autotools ExternalProjects (Python, ICU, OpenSSL) run their own
# configure/make and do NOT inherit these; they build uninstrumented, which is
# fine — the ASan runtime is linked into the final binary and mixed
# instrumented/uninstrumented code is supported.  See the header comment for
# the runtime knobs (ASAN_OPTIONS, PYTHONMALLOC=malloc).
if (RP_SANITIZE)
    set(RP_ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS             "${CMAKE_C_FLAGS} ${RP_ASAN_FLAGS}")
    set(CMAKE_CXX_FLAGS           "${CMAKE_CXX_FLAGS} ${RP_ASAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=address")
    message("###### RP_SANITIZE: AddressSanitizer enabled for the entire in-tree build ######")
endif (RP_SANITIZE)

message("###### CONFIGURING DEPENDENCIES ######")

include(extern/extern.cmake)

message("######   CONFIGURING RAMPART    ######")

add_subdirectory(src)

include(cmake/ExternalProject.cmake)

