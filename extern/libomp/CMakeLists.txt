# extern/libomp/CMakeLists.txt
#
# Builds a static `omp` library (libomp.a) from a vendored copy of the
# LLVM OpenMP runtime (llvmorg-22.1.1, openmp/runtime).  Used only on
# macOS, where Apple Clang does not ship libomp.  See NOTICE.md for the
# vendoring trim-list and LICENSE.txt for the upstream license.
#
# On every other platform supported by rampart (Linux, Linux 32-bit,
# FreeBSD 14, Windows MSYS2) the system toolchain ships OpenMP and the
# parent build uses find_package(OpenMP) instead of this directory.

if(NOT APPLE)
    return()
endif()

cmake_minimum_required(VERSION 3.18)

# Architecture detection — single-arch builds only (universal abandoned).
if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR
   CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(LIBOMP_ARCH_X86_64 0)
    set(LIBOMP_ARCH_AARCH64 1)
else()
    set(LIBOMP_ARCH_X86_64 1)
    set(LIBOMP_ARCH_AARCH64 0)
endif()

# Feature flags consumed by kmp_config.h.cmake.  These mirror what
# upstream's cmake/config-ix.cmake would pick on macOS for a minimal
# static build (no ITT, no OMPT, no debugger, no stats, no hwloc).
set(DEBUG_BUILD                            0)
set(RELWITHDEBINFO_BUILD                   0)
set(LIBOMP_USE_ITT_NOTIFY                  0)
set(LIBOMP_USE_VERSION_SYMBOLS             0)  # ELF-only feature
set(LIBOMP_HAVE_WEAK_ATTRIBUTE             1)  # clang/gcc support __attribute__((weak))
set(LIBOMP_HAVE_PSAPI                      0)  # Windows-only
set(LIBOMP_STATS                           0)
set(LIBOMP_HAVE_X86INTRIN_H                ${LIBOMP_ARCH_X86_64})
set(LIBOMP_HAVE___BUILTIN_READCYCLECOUNTER ${LIBOMP_ARCH_AARCH64})
set(LIBOMP_HAVE___RDTSC                    ${LIBOMP_ARCH_X86_64})
set(LIBOMP_USE_DEBUGGER                    0)
set(LIBOMP_OMPT_DEBUG                      0)
set(LIBOMP_OMPT_SUPPORT                    0)
set(LIBOMP_OMPD_SUPPORT                    0)
set(LIBOMP_OMPX_TASKGRAPH                  0)
set(LIBOMP_PROFILING_SUPPORT               0)
set(LIBOMP_OMPT_OPTIONAL                   0)
set(LIBOMP_USE_ADAPTIVE_LOCKS              ${LIBOMP_ARCH_X86_64})  # x86 TSX only
set(LIBOMP_USE_INTERNODE_ALIGNMENT         0)
set(LIBOMP_ENABLE_ASSERTIONS               0)
set(LIBOMP_USE_HIER_SCHED                  0)
set(STUBS_LIBRARY                          0)
set(LIBOMP_USE_HWLOC                       0)
set(LIBOMP_ENABLE_SHARED                   0)  # static library
if(LIBOMP_ARCH_AARCH64)
    set(LIBOMP_LEGAL_ARCH "AArch64")
else()
    set(LIBOMP_LEGAL_ARCH "Intel(R) 64")
endif()
set(LIBOMP_LIB_FILE                        "libomp.a")
set(LIBOMP_VERSION_MAJOR                   5)
set(LIBOMP_VERSION_MINOR                   0)
set(LIBOMP_VERSION_BUILD                   0)
string(TIMESTAMP LIBOMP_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
set(MSVC                                   0)
set(LIBOMP_HAVE_WAITPKG_INTRINSICS         0)
set(LIBOMP_HAVE_RTM_INTRINSICS             0)
set(LIBOMP_HAVE_IMMINTRIN_H                ${LIBOMP_ARCH_X86_64})
set(LIBOMP_HAVE_INTRIN_H                   0)  # MSVC-only
set(LIBOMP_HAVE_ATTRIBUTE_WAITPKG          0)
set(LIBOMP_HAVE_ATTRIBUTE_RTM              0)
set(LIBOMP_ARCH_AARCH64_A64FX              0)
set(LIBOMP_HAVE_XMMINTRIN_H                ${LIBOMP_ARCH_X86_64})
set(LIBOMP_HAVE__MM_MALLOC                 ${LIBOMP_ARCH_X86_64})
set(LIBOMP_HAVE_ALIGNED_ALLOC              0)  # macOS aligned_alloc is 10.15+, deployment target is 11.0 — but stick with posix_memalign for breadth
set(LIBOMP_HAVE_POSIX_MEMALIGN             1)
set(LIBOMP_HAVE__ALIGNED_MALLOC            0)  # Windows-only
set(OPENMP_ENABLE_LIBOMPTARGET             0)

# Generate kmp_config.h and the user-facing OpenMP headers.
set(LIBOMP_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR})
configure_file(src/kmp_config.h.cmake     ${LIBOMP_BIN_DIR}/kmp_config.h @ONLY)
configure_file(src/include/omp.h.var      ${LIBOMP_BIN_DIR}/omp.h        @ONLY)
configure_file(src/include/ompx.h.var     ${LIBOMP_BIN_DIR}/ompx.h       @ONLY)

# Source list — Unix path, no OMPT/ITT/STATS/DEBUGGER, no taskgraph.
set(LIBOMP_CXX_SOURCES
    src/kmp_alloc.cpp
    src/kmp_atomic.cpp
    src/kmp_csupport.cpp
    src/kmp_debug.cpp
    src/kmp_itt.cpp
    src/kmp_environment.cpp
    src/kmp_error.cpp
    src/kmp_global.cpp
    src/kmp_i18n.cpp
    src/kmp_io.cpp
    src/kmp_runtime.cpp
    src/kmp_settings.cpp
    src/kmp_str.cpp
    src/kmp_tasking.cpp
    src/kmp_threadprivate.cpp
    src/kmp_utility.cpp
    src/kmp_barrier.cpp
    src/kmp_wait_release.cpp
    src/kmp_affinity.cpp
    src/kmp_dispatch.cpp
    src/kmp_lock.cpp
    src/kmp_sched.cpp
    src/kmp_collapse.cpp
    src/z_Linux_util.cpp
    src/kmp_gsupport.cpp
    src/kmp_taskdeps.cpp
    src/kmp_cancel.cpp
    src/kmp_ftn_cdecl.cpp
    src/kmp_ftn_extra.cpp
    src/kmp_version.cpp
)
set(LIBOMP_ASM_SOURCES src/z_Linux_asm.S)

add_library(omp STATIC ${LIBOMP_CXX_SOURCES} ${LIBOMP_ASM_SOURCES})

set_target_properties(omp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD              17
    CXX_STANDARD_REQUIRED     ON
    CXX_EXTENSIONS            OFF
    LINKER_LANGUAGE           CXX
)

target_include_directories(omp
    PUBLIC
        $<BUILD_INTERFACE:${LIBOMP_BIN_DIR}>          # generated omp.h, ompx.h, kmp_config.h
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/i18n
)

target_compile_definitions(omp PRIVATE
    _GNU_SOURCE
    _REENTRANT
)

# Silence the wall of warnings the upstream sources emit at -Wall.
target_compile_options(omp PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-deprecated-declarations
    -Wno-cast-qual
    -Wno-format-pedantic
    -Wno-unknown-pragmas
    -Wno-missing-field-initializers
    -Wno-switch
    -Wno-extra
)

# Make z_Linux_asm.S preprocess as C — it #include's kmp_config.h.
set_source_files_properties(${LIBOMP_ASM_SOURCES} PROPERTIES
    LANGUAGE C
)
