find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(texisapi PUBLIC ${MATH_LIBRARY})
endif()
find_library(DYNAMIC_LINK_LIBRARY dl)
if(DYNAMIC_LINK_LIBRARY)
  target_link_libraries(texisbuild PUBLIC ${DYNAMIC_LINK_LIBRARY})
  target_link_libraries(texisapi PUBLIC ${DYNAMIC_LINK_LIBRARY})
endif()
#
# EPI_HAVE_PTHREADS
#
set(THREADS_PREFER_PTHREADS_FLAG ON)
find_package(Threads REQUIRED)
IF(Threads_FOUND)
	SET(EPI_HAVE_PTHREADS Threads_FOUND)
	CHECK_SYMBOL_EXISTS(PTHREAD_MUTEX_INITIALIZER	pthread.h	HAVE_PTHREAD_MUTEX_INITIALIZER)
ENDIF(Threads_FOUND)
if(HAVE_RE2)
target_link_libraries(re2 PUBLIC Threads::Threads)
endif(HAVE_RE2)
#
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  target_link_libraries(texisbuild PUBLIC kvm)
  target_link_libraries(texisapi PUBLIC kvm)	
endif()

add_executable(rex ${TEXIS_SOURCE_DIR}/texisapi/rex.c)
target_compile_definitions(rex PRIVATE TEST=1)
set(REX_EXE  ${TEXIS_BINARY_DIR}/apps/rex PARENT_SCOPE)

add_executable(addtable atmain.c)
add_executable(kdbfchk kcmain.c)
add_executable(metamorph metamorph.c)
add_executable(backref backrefmain.c)
add_executable(tsql
	tsqlmain.c
  tsql.c
	adduser.c
	admin.c
##WTF	getpass.c
	getsql.c
	tup_cdsp.c
	tup_disp.c
	)
set(TSQL_EXE  ${TEXIS_BINARY_DIR}/apps/tsql PARENT_SCOPE)

# Tools that don't run SQL / don't actually call vec dispatch link the
# lean `texisbuild` (no vec sources, no faiss, no openblas / libgomp /
# libgfortran chain in DT_NEEDED).  See extern/texis/texisapi/CMakeLists.txt
# for the BUILD_SRCS/VEC_SRCS split and txdummyver.c for the vec stubs
# that make the lean link succeed.
target_link_libraries(rex       LINK_PUBLIC texisbuild)
target_link_libraries(metamorph LINK_PUBLIC texisbuild)
target_link_libraries(backref   LINK_PUBLIC texisbuild)

# tsql can `CREATE VECTOR INDEX backend='ivfpq'` — needs the real vec
# code paths.  Links the full texisapi.
target_link_libraries(tsql      PUBLIC Threads::Threads)
target_link_libraries(tsql      LINK_PUBLIC texisapi)

# CMake 3.16 (the Buster Pi's cmake) doesn't propagate texisapi's
# PUBLIC link to faiss_ivfpq + OpenMP + BLAS through to tsql via
# INTERFACE_LINK_LIBRARIES, so tsql ends up with undefined faiss/
# GOMP/cblas references at link time.  Wire them directly on the
# armhf+GCC<9 workaround path.  Modern CMake on every other platform
# carries them transitively without this block.
if(RP_ARMHF_GCC8_FAISS_WORKAROUND)
    find_package(OpenMP REQUIRED)
    if(OpenMP_CXX_FOUND)
        find_package(BLAS REQUIRED)
        find_package(LAPACK)
        target_link_libraries(tsql PRIVATE
            -Wl,--whole-archive $<TARGET_FILE:faiss_ivfpq> -Wl,--no-whole-archive
            OpenMP::OpenMP_CXX ${BLAS_LIBRARIES} ${LAPACK_LIBRARIES})
        add_dependencies(tsql faiss_ivfpq)
    endif()
endif()

# RPATH $ORIGIN/../lib lets tsql's hard DT_NEEDED on the BLAS chain
# (libopenblas/libgomp/libgfortran — an EXECUTABLE cannot leave those
# unresolved for vec_blas_probe() the way rampart-sql.so does) be
# satisfied by the bundled copies at <prefix>/lib/.  Crucial for the
# docker/oven installs onto vanilla systems (no system openblas);
# harmless elsewhere (system loader cache is the fallback).
set_target_properties(tsql PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../lib"
    BUILD_RPATH   "\$ORIGIN/../lib"
)

# addtable (adds an existing table to the catalog) and kdbfchk (DB
# file integrity check) predate vector indexes by decades.  Even
# though their pulled SQL plan code has strong refs to vec dispatch
# symbols, those code paths aren't actually exercised at runtime — the
# stubs in txdummyver.c cover them harmlessly.  Switching to texisbuild
# drops the openblas/libgomp/libgfortran chain from their DT_NEEDED.
target_link_libraries(addtable  PUBLIC Threads::Threads)
target_link_libraries(addtable  LINK_PUBLIC texisbuild)
target_link_libraries(kdbfchk   PUBLIC Threads::Threads)
target_link_libraries(kdbfchk   LINK_PUBLIC texisbuild)

if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS rex tsql RUNTIME DESTINATION bin)
else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS rex tsql RUNTIME)
endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

if(LOCK_SERVER AND EVENT_LIBRARY)
add_executable(texislockd
  lockd_main.c
)
target_link_libraries(texislockd LINK_PUBLIC event_core)
# texislockd is a lock daemon — links the lean texisbuild (no vec,
# no faiss).  Loads on a vanilla install with no scientific libs.
target_link_libraries(texislockd LINK_PUBLIC texisbuild)
# RPATH $ORIGIN/../lib lets texislockd find the bundled libopenblas /
# libgomp / libgfortran / libquadmath when installed via the
# rampart-install bundle (which extracts them at <prefix>/lib/),
# while still falling back to the system loader cache for non-bundle
# installs.
set_target_properties(texislockd PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../lib"
    BUILD_RPATH   "\$ORIGIN/../lib"
)
set(LOCKD_EXE  ${TEXIS_BINARY_DIR}/apps/texislockd PARENT_SCOPE)

#if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
#  install(TARGETS texislockd RUNTIME DESTINATION bin)
#else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
#  install(TARGETS texislockd RUNTIME)
#endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

endif(LOCK_SERVER AND EVENT_LIBRARY)

if(TEXIS_SILENCE_AUDITED_WARNINGS)
    # daemon() works fine on macOS 11; Apple just deprecated it in favor
    # of posix_spawn for new code.  Single-file suppression keeps the
    # deprecation warning enabled everywhere else.
    set_source_files_properties(lockd_main.c
        PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
endif()
