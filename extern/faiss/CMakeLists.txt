# extern/faiss/CMakeLists.txt
#
# Builds a static library `faiss_ivfpq` from a pruned subset of FAISS.
# See NOTICE.md for what is and isn't vendored.
#
# Output:
#   faiss_ivfpq          static lib, links into texisapi
#   include path         this directory (so vendored code's
#                        `#include <faiss/...>` resolves)
#
# Dependencies (build-time):
#   BLAS / LAPACK        macOS: Accelerate framework (free)
#                        Linux: libopenblas-dev or libblas-dev (apt)
#   OpenMP               compiler-bundled (gcc/clang)

cmake_minimum_required(VERSION 3.18)

# C++17 — FAISS requires it; matches existing usearch in texisapi.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files (32 .cpp, computed via the include-closure scan; see NOTICE.md).
set(FAISS_IVFPQ_SOURCES
    faiss/Clustering.cpp
    faiss/Index.cpp
    faiss/IndexFlat.cpp
    faiss/IndexFlatCodes.cpp
    faiss/IndexIVF.cpp
    faiss/IndexIVFPQ.cpp
    faiss/IndexPQ.cpp
    faiss/VectorTransform.cpp
    faiss/impl/AuxIndexStructures.cpp
    faiss/impl/CodePacker.cpp
    faiss/impl/FaissException.cpp
    faiss/impl/IDSelector.cpp
    faiss/impl/PolysemousTraining.cpp
    faiss/impl/ProductQuantizer.cpp
    faiss/impl/io.cpp
    faiss/impl/kmeans1d.cpp
    faiss/invlists/BlockInvertedLists.cpp
    faiss/invlists/DirectMap.cpp
    faiss/invlists/InvertedLists.cpp
    faiss/invlists/InvertedListsIOHook.cpp
    faiss/invlists/OnDiskInvertedLists.cpp
    faiss/utils/Heap.cpp
    faiss/utils/distances.cpp
    faiss/utils/distances_simd.cpp           # SIMD impls of fvec_L1 / Linf / L2sqr / inner_product
    faiss/utils/distances_fused/avx512.cpp
    faiss/utils/distances_fused/distances_fused.cpp
    faiss/utils/distances_fused/simdlib_based.cpp
    faiss/utils/extra_distances.cpp
    faiss/utils/hamming.cpp
    faiss/utils/partitioning.cpp
    faiss/utils/random.cpp
    faiss/utils/sorting.cpp
    faiss/utils/utils.cpp
)

add_library(faiss_ivfpq STATIC ${FAISS_IVFPQ_SOURCES})

# Vendored sources `#include <faiss/...>` — make this dir the include root.
target_include_directories(faiss_ivfpq
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# FINTEGER is BLAS's Fortran integer type.  Upstream FAISS sets it to
# `int` for all non-Windows / non-MKL configurations.
target_compile_definitions(faiss_ivfpq PRIVATE FINTEGER=int)

# Position-independent code: this static lib gets linked into texisapi,
# which is itself loaded into rampart's shared modules.
set_target_properties(faiss_ivfpq PROPERTIES POSITION_INDEPENDENT_CODE ON)

# NOTE: do NOT set CXX_VISIBILITY_PRESET=hidden here.  C++ RTTI
# (typeinfo / vtable symbols) for classes used across the lib boundary
# would become invisible to vecindex_ivfpq.cpp in libtexisapi, leading
# to runtime "undefined symbol: _ZTIN5faiss5IndexE" failures when
# rampart-sql.so loads.  FAISS has many polymorphic types — the cost
# of restricting visibility per-class is high and the namespace clash
# with rampart-langtools' separate FAISS copy is benign in practice
# (each .so has its own copy).

# Release-style optimization.  Honour parent CMAKE_BUILD_TYPE; if not
# set, force Release since FAISS code is heavily optimization-sensitive.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Quiet down a few warnings emitted by FAISS sources at -Wall.
target_compile_options(faiss_ivfpq PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)

# OpenMP — FAISS uses #pragma omp throughout (training, search, etc.).
find_package(OpenMP REQUIRED)
target_link_libraries(faiss_ivfpq PUBLIC OpenMP::OpenMP_CXX)

# BLAS — macOS gets Accelerate for free; Linux strongly prefers OpenBLAS
# (FAISS training is ~10× slower with the reference / Netlib BLAS).
if(APPLE)
    target_link_libraries(faiss_ivfpq PUBLIC "-framework Accelerate")
    target_compile_definitions(faiss_ivfpq PRIVATE
        ACCELERATE_NEW_LAPACK
        ACCELERATE_LAPACK_ILP64=0
    )
else()
    # Try OpenBLAS first via BLA_VENDOR; fall back to whatever BLAS is
    # findable, with a clear warning about the perf cost.
    set(BLA_VENDOR OpenBLAS)
    find_package(BLAS QUIET)
    if(NOT BLAS_FOUND)
        unset(BLA_VENDOR)
        find_package(BLAS REQUIRED)
        message(WARNING
            "FAISS: OpenBLAS not found; falling back to ${BLAS_LIBRARIES}.\n"
            "  Index training (CREATE VECTOR INDEX with backend='ivfpq') may\n"
            "  be ~10× slower with reference BLAS than with OpenBLAS.\n"
            "  Recommended: apt install libopenblas-dev  (Debian/Ubuntu)\n"
            "          or:  dnf install openblas-devel   (Fedora/RHEL)")
    else()
        message(STATUS "FAISS: using OpenBLAS at ${BLAS_LIBRARIES}")
    endif()
    find_package(LAPACK)
    target_link_libraries(faiss_ivfpq PUBLIC ${BLAS_LIBRARIES})
    if(LAPACK_FOUND)
        target_link_libraries(faiss_ivfpq PUBLIC ${LAPACK_LIBRARIES})
    endif()
endif()

# Threads (FAISS uses std::thread / pthreads in places).
find_package(Threads REQUIRED)
target_link_libraries(faiss_ivfpq PUBLIC Threads::Threads)
