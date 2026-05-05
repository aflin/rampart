set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)


# FAISS needs OpenMP.  Apple Clang doesn't ship it, so on macOS we
# build a static libomp from the vendored LLVM 22.1.1 runtime sources
# under extern/libomp/.  On every other supported platform (Linux,
# FreeBSD, MSYS2) the system toolchain provides OpenMP and we fall
# through to find_package(OpenMP) with no overrides.
#
# Pre-filling the OpenMP_* cache vars makes the find_package(OpenMP)
# call inside extern/faiss/CMakeLists.txt pick up our static lib
# without searching the system.
if(APPLE)
    add_subdirectory(${EXTERN_DIR}/libomp)
    set(OpenMP_omp_LIBRARY   "$<TARGET_FILE:omp>"
        CACHE STRING "OpenMP_omp_LIBRARY"   FORCE)
    set(OpenMP_C_FLAGS       "-Xpreprocessor -fopenmp -I${CMAKE_BINARY_DIR}/extern/libomp"
        CACHE STRING "OpenMP_C_FLAGS"       FORCE)
    set(OpenMP_C_LIB_NAMES   "omp"
        CACHE STRING "OpenMP_C_LIB_NAMES"   FORCE)
    set(OpenMP_CXX_FLAGS     "-Xpreprocessor -fopenmp -I${CMAKE_BINARY_DIR}/extern/libomp"
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

# Everything below this point is vendored upstream code we don't
# maintain (openssl, oniguruma, curl, libevent, libevhtp_ws, tidy-html5,
# cmark-gfm, robotstxt).  Wrap with the SILENCE_VENDORED_WARNINGS toggle
# so warning noise from these libraries doesn't drown out warnings from
# the rampart code we actually own.  Restored at the bottom of the file.
if(SILENCE_VENDORED_WARNINGS)
    set(_VENDORED_SAVED_C_FLAGS   "${CMAKE_C_FLAGS}")
    set(_VENDORED_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -w")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
endif()

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

if(SILENCE_VENDORED_WARNINGS)
    set(CMAKE_C_FLAGS   "${_VENDORED_SAVED_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${_VENDORED_SAVED_CXX_FLAGS}")
endif()
