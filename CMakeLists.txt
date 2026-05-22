## to build a macos universal, run:
#   cmake -DMACOS_UNIVERSAL=1 ../

## to build with -fsanitize=address
#   cmake -DRP_SANITIZE=1 ../
# and maybe export ASAN_OPTIONS=fast_unwind_on_malloc=false
# if stack traces are incomplete.
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
set(TEXIS_AUDITED_WARNING_FLAGS
    -Wno-unsequenced                   # 147x: macros mutate shared state across one expr
    -Wno-enum-compare                  #  39x: intentional cross-enum compares (op codes)
    -Wno-return-type                   #   8x: unreachable fall-through paths
    -Wno-switch                        #   6x: enum switch doesn't cover every value
    -Wno-dangling-else                 #   4x: stylistic if-else nesting
    -Wno-unused-value                  #   2x: comma-op macro fragments
    -Wno-string-plus-int               #   2x: intentional pointer-into-string arithmetic
    -Wno-tautological-constant-compare #   1x: constant-shift always-true
    -Wno-pointer-to-int-cast           #   1x: pthread_t truncation for logging only
    -Wno-extern-c-compat               #   1x: empty union size differs C vs C++
)

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

message("###### CONFIGURING DEPENDENCIES ######")

include(extern/extern.cmake)

message("######   CONFIGURING RAMPART    ######")

add_subdirectory(src)

include(cmake/ExternalProject.cmake)

