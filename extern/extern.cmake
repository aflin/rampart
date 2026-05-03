set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)


# FAISS needs OpenMP, and Apple Clang doesn't ship it.  Use Homebrew's
# libomp and pre-fill the cache vars so the find_package(OpenMP) call
# inside extern/faiss/CMakeLists.txt finds it without further hints.
# Mirrors the rampart-langtools setup (extern/extern.cmake there).
# Static-linking libomp.a into the .so means no runtime dep on Homebrew.
if(APPLE)
    execute_process(
        COMMAND brew --prefix libomp
        OUTPUT_VARIABLE OMP_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT OMP_PREFIX)
        message(FATAL_ERROR "OpenMP not found; try: brew install libomp")
    endif()
    set(OpenMP_omp_LIBRARY   "${OMP_PREFIX}/lib/libomp.a"
        CACHE STRING "OpenMP_omp_LIBRARY"   FORCE)
    set(OpenMP_C_FLAGS       "-Xpreprocessor -fopenmp -I${OMP_PREFIX}/include"
        CACHE STRING "OpenMP_C_FLAGS"       FORCE)
    set(OpenMP_C_LIB_NAMES   "omp"
        CACHE STRING "OpenMP_C_LIB_NAMES"   FORCE)
    set(OpenMP_CXX_FLAGS     "-Xpreprocessor -fopenmp -I${OMP_PREFIX}/include"
        CACHE STRING "OpenMP_CXX_FLAGS"     FORCE)
    set(OpenMP_CXX_LIB_NAMES "omp"
        CACHE STRING "OpenMP_CXX_LIB_NAMES" FORCE)
endif()


# FAISS (pruned subset, IVFPQ-only) — must be declared before texis
# so the `faiss_ivfpq` target exists when texisapi links against it.
# See extern/faiss/NOTICE.md for what's vendored.
add_subdirectory(${EXTERN_DIR}/faiss EXCLUDE_FROM_ALL)

add_subdirectory(${EXTERN_DIR}/texis)

target_compile_definitions(
    texisapi PRIVATE
    RAMPART_INCLUDE_TEXIS_USERFUNC="${CMAKE_SOURCE_DIR}/src/duktape/modules/sql-userfunc.c"
)

add_subdirectory(${EXTERN_DIR}/openssl)

add_subdirectory(${EXTERN_DIR}/oniguruma)

if(CYGWIN)
  # GCC 15 defaults to C23 where 0 is not a null pointer constant,
  # causing curl's cmake recv/send checks to fail. Force POSIX signatures.
  set(curl_cv_recv 1 CACHE INTERNAL "")
  set(curl_cv_func_recv_args "int,void *,size_t,int,ssize_t" CACHE INTERNAL "")
  set(RECV_TYPE_ARG1 "int" CACHE INTERNAL "")
  set(RECV_TYPE_ARG2 "void *" CACHE INTERNAL "")
  set(RECV_TYPE_ARG3 "size_t" CACHE INTERNAL "")
  set(RECV_TYPE_ARG4 "int" CACHE INTERNAL "")
  set(RECV_TYPE_RETV "ssize_t" CACHE INTERNAL "")
  set(HAVE_RECV 1 CACHE INTERNAL "")
  set(curl_cv_send 1 CACHE INTERNAL "")
  set(curl_cv_func_send_args "int,void *,size_t,int,ssize_t,const" CACHE INTERNAL "")
  set(SEND_TYPE_ARG1 "int" CACHE INTERNAL "")
  set(SEND_TYPE_ARG2 "void *" CACHE INTERNAL "")
  set(SEND_TYPE_ARG3 "size_t" CACHE INTERNAL "")
  set(SEND_TYPE_ARG4 "int" CACHE INTERNAL "")
  set(SEND_TYPE_RETV "ssize_t" CACHE INTERNAL "")
  set(SEND_QUAL_ARG2 "const" CACHE INTERNAL "")
  set(HAVE_SEND 1 CACHE INTERNAL "")
endif()
add_subdirectory(${EXTERN_DIR}/curl)

include_directories(${CMAKE_BINARY_DIR}/extern/oniguruma/include)

add_subdirectory(${EXTERN_DIR}/libevent)

add_subdirectory(${EXTERN_DIR}/libevhtp_ws)

add_subdirectory(${EXTERN_DIR}/tidy-html5)

add_subdirectory(${EXTERN_DIR}/cmark-gfm)

add_subdirectory(${EXTERN_DIR}/robotstxt)
