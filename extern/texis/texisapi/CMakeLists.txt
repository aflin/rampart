include (CheckCSourceCompiles)
include (CheckCSourceRuns)
include(CheckIncludeFile)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(CheckTypeSize)
include(FindThreads)
include(${TEXIS_SOURCE_DIR}/cmake/FindParsers.cmake)
#include(FindLibXml2)
add_definitions(-DFORMMAPI -DEPI_NO_VERSION_FUNCTIONS)

# Detect 32-bit ARM for the standalone-texis build path (the parent
# rampart build also detects this in extern/extern.cmake; we duplicate
# here so `cmake ../../../extern/texis` works too).  RP_NO_FAISS gates
# vecindex_ivfpq.cpp out of the source list and skips the faiss_ivfpq
# link below.  See extern/extern.cmake for the rationale.
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT DEFINED RP_NO_FAISS)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
        set(RP_ARMHF_GCC8_FAISS_WORKAROUND ON CACHE INTERNAL
            "Build FAISS on armhf with pre-CXX11 std::string ABI")
    else()
        set(RP_NO_FAISS ON CACHE INTERNAL "FAISS unsupported on 32-bit ARM")
    endif()
endif()
if(RP_NO_FAISS)
    add_definitions(-DRP_NO_FAISS)
endif()

set(TEXISAPI_BINARY_DIR "${TEXIS_BINARY_DIR}/texisapi")
set(TEXISAPI_SOURCE_DIR "${TEXIS_SOURCE_DIR}/texisapi")

#
# How many args does getpwuid take?
#
include(${TEXIS_SOURCE_DIR}/cmake/CheckHasGetPWUID.cmake)
if(HAVE_GETPWUID_5)
	SET(EPI_HAVE_GETPWUID_R_INT 1)
endif (HAVE_GETPWUID_5)
if(HAVE_GETPWUID_4)
	SET(EPI_HAVE_GETPWUID_R_PASSWD 1)
endif(HAVE_GETPWUID_4)

#
# Make sure we are using the same defines as the project
#
get_property(compiledefs DIRECTORY PROPERTY COMPILE_DEFINITIONS)
list(TRANSFORM compiledefs PREPEND " -D")
set(CMAKE_REQUIRED_DEFINITIONS ${compiledefs})
#
CHECK_INCLUDE_FILE("arpa/inet.h"	EPI_HAVE_ARPA_INET_H)
CHECK_INCLUDE_FILE("arpa/nameser.h" EPI_HAVE_ARPA_NAMESER_H)
CHECK_INCLUDE_FILE("execinfo.h"	EPI_HAVE_EXECINFO_H)
CHECK_INCLUDE_FILE("io.h"				EPI_HAVE_IO_H)
CHECK_INCLUDE_FILE("kvm.h" 			HAVE_KVM_H)
CHECK_INCLUDE_FILE("netdb.h"		EPI_HAVE_NETDB_H)
CHECK_INCLUDE_FILE("pwd.h"			HAVE_PWD_H)
CHECK_INCLUDE_FILE("stdarg.h" 	EPI_HAVE_STDARG)
CHECK_INCLUDE_FILE("strings.h" 	HAVE_STRINGS_H)
CHECK_INCLUDE_FILE("sys/sem.h"  EPI_HAVE_SYS_SEM_H)
CHECK_INCLUDE_FILE("sys/shm.h"  HAVE_SYS_SHM_H)
CHECK_INCLUDE_FILE("sys/un.h"   HAVE_SYS_UN_H)
CHECK_INCLUDE_FILE("sys/user.h" HAVE_SYS_USER_H)
CHECK_INCLUDE_FILE("unistd.h" 	EPI_HAVE_UNISTD_H)
IF(NOT CYGWIN)
CHECK_INCLUDE_FILE("windows.h"  HAVE_WINDOWS_H)

IF(HAVE_WINDOWS_H)
  set(WINDOWS_INCLUDE_FILE "windows.h")
  set(CMAKE_EXTRA_INCLUDE_FILES ${WINDOWS_INCLUDE_FILE})
ENDIF(HAVE_WINDOWS_H)
ELSE()
  # Force HAVE_WINDOWS_H off for Cygwin/MSYS - the cached value from
  # a previous configuration may still be set and would be written
  # into txcoreconfig.h, causing type conflicts with Windows headers.
  set(HAVE_WINDOWS_H "" CACHE INTERNAL "" FORCE)
ENDIF(NOT CYGWIN)


CHECK_C_SOURCE_COMPILES("main(){int x = alignof(x);}" EPI_HAVE_ALIGNOF)
CHECK_C_SOURCE_COMPILES("main(){int x = _alignof(x);}" EPI_HAVE__ALIGNOF)
CHECK_C_SOURCE_COMPILES("main(){int x = __alignof(x);}" EPI_HAVE___ALIGNOF)
CHECK_C_SOURCE_COMPILES("main(){int x = __alignof__(x);}" EPI_HAVE___ALIGNOF__)
CHECK_C_SOURCE_COMPILES("main(){int x = 5, res; res = __atomic_fetch_add(&x, 3, __ATOMIC_SEQ_CST); }" EPI_HAVE_ATOMIC_FETCH_FUNCTIONS)
CHECK_C_SOURCE_COMPILES("main(){int x = 5, res; res = __sync_fetch_and_add(&x, 3); }" EPI_HAVE_SYNC_FETCH_FUNCTIONS)

IF(EPI_HAVE_UNISTD_H)
	set(CMAKE_EXTRA_INCLUDE_FILES ${CMAKE_EXTRA_INCLUDE_FILES} "unistd.h")
ENDIF(EPI_HAVE_UNISTD_H)

CHECK_C_SOURCE_RUNS("#include <fcntl.h>\nmain(){int fd; fd = open(\"${TEXIS_SOURCE_DIR}/CMakeLists.txt\", O_RDONLY); if(fd<0) return 2; fd = open(\"${TEXIS_SOURCE_DIR}/cmakelists.txt\", O_RDONLY); if(fd<0) return 0; return 1;}" FS_CASE_INSENSITIVE)
if("${FS_CASE_INSENSITIVE}" EQUAL 0)
	set(EPI_CASE_INSENSITIVE_PATHS)
endif("${FS_CASE_INSENSITIVE}" EQUAL 0)
#
CHECK_SYMBOL_EXISTS(alignof 	stdio.h 	EPI_HAVE_ALIGNOF)
CHECK_SYMBOL_EXISTS(_alignof 	stdio.h 	EPI_HAVE__ALIGNOF)
CHECK_SYMBOL_EXISTS(__alignof stdio.h 	EPI_HAVE___ALIGNOF)
CHECK_SYMBOL_EXISTS(bzero     strings.h 	EPI_HAVE_BZERO)
#
# Dynamic Link Library
#
SET(CMAKE_REQUIRED_LIBRARIES dl)
CHECK_SYMBOL_EXISTS(dlopen		dlfcn.h		EPI_HAVE_DLOPEN)
SET(CMAKE_REQUIRED_LIBRARIES )
#
# Various standard functions
#
CHECK_SYMBOL_EXISTS(ecvt		stdlib.h	EPI_HAVE_ECVT)
CHECK_SYMBOL_EXISTS(fcvt		stdlib.h	EPI_HAVE_FCVT)
CHECK_SYMBOL_EXISTS(fdatasync	unistd.h	EPI_HAVE_FDATASYNC)
CHECK_SYMBOL_EXISTS(fsync		unistd.h	EPI_HAVE_FSYNC)
CHECK_SYMBOL_EXISTS(gcvt		stdlib.h	EPI_HAVE_GCVT)
CHECK_SYMBOL_EXISTS(getgroups	unistd.h	EPI_HAVE_GETGROUPS)
CHECK_SYMBOL_EXISTS(getpagesize	unistd.h EPI_HAVE_GETPAGESIZE)
CHECK_SYMBOL_EXISTS(getpass		unistd.h	HAVE_GETPASS_DECL)
CHECK_SYMBOL_EXISTS(hstrerror	netdb.h		EPI_HAVE_HSTRERROR)
CHECK_SYMBOL_EXISTS(task_info	mach/task_info.h HAVE_MACH_TASK_INFO)
CHECK_SYMBOL_EXISTS(madvise		sys/mman.h	EPI_HAVE_MADVISE)
CHECK_SYMBOL_EXISTS(mmap			sys/mman.h	EPI_HAVE_MMAP)
CHECK_SYMBOL_EXISTS(msync			sys/mman.h	EPI_HAVE_MSYNC)
CHECK_C_SOURCE_RUNS("#include <stdlib.h>\nint f(const void *a, const void *b, void *c){return ((*(int *)a)-(*(int *)b))*(*(int *)c);}\nint main() {int i[]={1,2};int r=-1;qsort_r(i,2,sizeof(int),f,&r);if((i[0]==2)&&(i[1]==1)){return 0;}return -1;}" QSORT_R_WORKS)
IF("${QSORT_R_WORKS}" EQUAL 0)
	set(EPI_HAVE_QSORT_R ON)
ENDIF("${QSORT_R_WORKS}" EQUAL 0)
#CHECK_SYMBOL_EXISTS(qsort_r		stdlib.h		EPI_HAVE_QSORT_R)
CHECK_SYMBOL_EXISTS(qsort_s		stdlib.h;search.h		EPI_HAVE_QSORT_S)
CHECK_SYMBOL_EXISTS(snprintf	stdio.h		EPI_HAVE_SNPRINTF)
CHECK_SYMBOL_EXISTS(statvfs		sys/statvfs.h	EPI_HAVE_STATVFS)
IF(EPI_HAVE_STATVFS)
	CHECK_STRUCT_HAS_MEMBER("struct statvfs"	f_bavail sys/statvfs.h EPI_HAVE_STATVFS_BAVAIL)
ENDIF(EPI_HAVE_STATVFS)
CHECK_SYMBOL_EXISTS(strerror 	string.h 	HAVE_STRERROR)
CHECK_SYMBOL_EXISTS(strncasecmp strings.h 	EPI_HAVE_STRNCASECMP)
CHECK_SYMBOL_EXISTS(strnicmp 	string.h 	EPI_HAVE_STRNICMP)
#
# HAVE VARIOUS UNSIGNED AND LONG TYPES
#
CHECK_TYPE_SIZE("uint"	UINT)
CHECK_TYPE_SIZE("long long" LONG_LONG)
IF(HAVE_LONG_LONG)
		SET(EPI_HAVE_LONG_LONG HAVE_LONG_LONG)
ENDIF(HAVE_LONG_LONG)
CHECK_TYPE_SIZE("unsigned long long" UNSIGNED_LONG_LONG)
IF(HAVE_UNSIGNED_LONG_LONG)
		SET(EPI_HAVE_UNSIGNED_LONG_LONG HAVE_UNSIGNED_LONG_LONG)
ENDIF(HAVE_UNSIGNED_LONG_LONG)
CHECK_TYPE_SIZE("long double" LONG_DOUBLE)
IF(HAVE_LONG_DOUBLE)
		SET(EPI_HAVE_LONG_DOUBLE HAVE_LONG_DOUBLE)
ENDIF(HAVE_LONG_DOUBLE)
CHECK_TYPE_SIZE("ssize_t" SSIZE_T)
IF(HAVE_SSIZE_T)
		SET(EPI_HAVE_SIGNED_SSIZE_T HAVE_SSIZE_T)
ENDIF(HAVE_SSIZE_T)
IF(NOT CYGWIN)
CHECK_TYPE_SIZE("__int64" __INT64_T)
IF(HAVE___INT64_T)
		SET(EPI_HAVE___INT64 HAVE___INT64_T)
ENDIF(HAVE___INT64_T)
ENDIF(NOT CYGWIN)
#
# __FLOAT128 Support is currently incomplete and experimental
# Force a variable to be set to check for and compile
#
IF(WANT_FLOAT128)
	CHECK_TYPE_SIZE("__float128" __FLOAT128)
	IF(HAVE___FLOAT128)
			CHECK_C_SOURCE_COMPILES("#include <math.h>\n int main() { __float128 g; return isinf(g);}" EPI_HAVE___FLOAT128)
#			SET(EPI_HAVE___FLOAT128 EPI_HAVE___FLOAT128)
	ENDIF(HAVE___FLOAT128)
ENDIF(WANT_FLOAT128)
CHECK_TYPE_SIZE("__uint64" __UINT64_T)
IF(HAVE___UINT64_T)
		SET(EPI_HAVE___UINT64E_T HAVE___UINT64_T)
ENDIF(HAVE___UINT64_T)
CHECK_C_SOURCE_COMPILES("#include <stdio.h>\n main() { size_t st = 0; printf(\"%zu\", st);}" EPI_HAVE_SIZE_T_ZU_FORMAT)
CHECK_C_SOURCE_COMPILES("#include <stdio.h>\n main() { size_t st = 0; printf(\"%lu\", st);}" EPI_HAVE_SIZE_T_LU_FORMAT)
CHECK_C_SOURCE_COMPILES("#include <stdio.h>\n main() { size_t st = 0; printf(\"%u\", st);}" EPI_HAVE_SIZE_T_U_FORMAT)
#
# Check Math functions
#
SET(CMAKE_REQUIRED_LIBRARIES m)
CHECK_SYMBOL_EXISTS(isinf		math.h	EPI_HAVE_ISINF_CALL)
CHECK_SYMBOL_EXISTS(isnan		math.h	EPI_HAVE_ISNAN_CALL)
CHECK_SYMBOL_EXISTS(nan			math.h	EPI_HAVE_NAN_CALL)
CHECK_SYMBOL_EXISTS(nanl		math.h	EPI_HAVE_NANL_CALL)
#
# HAVE_PID_T
#
CHECK_TYPE_SIZE(pid_t       PID_T)
IF(HAVE_PID_T)
  SET(pid_t pid_t)
ELSE(HAVE_PID_T)
  IF(WIN32)
    SET(pid_t "int")
  ELSE(WIN32)
    MESSAGE(FATAL_ERROR "pid_t doesn't exist on this platform?")
  ENDIF(WIN32)
ENDIF(HAVE_PID_T)
#
# HAVE_RLIM_T
#
SET(CMAKE_EXTRA_INCLUDE_FILES sys/resource.h)
CHECK_TYPE_SIZE(rlim_t HAVE_RLIM_T)
IF(HAVE_RLIM_T)
	SET(EPI_HAVE_RLIM_T HAVE_RLIM_T)
ENDIF(HAVE_RLIM_T)
#
# Signal Handling
#
CHECK_SYMBOL_EXISTS(sigaction	signal.h	EPI_HAVE_SIGACTION)
CHECK_STRUCT_HAS_MEMBER("struct sigaction" sa_sigaction signal.h
                        EPI_HAVE_SA_SIGACTION LANGUAGE C)
SET(CMAKE_EXTRA_INCLUDE_FILES signal.h)
CHECK_TYPE_SIZE(siginfo_t SIGINFO_T)
IF(HAVE_SIGINFO_T)
	SET(EPI_HAVE_SIGINFO_T	HAVE_SIGINFO_T)
ENDIF(HAVE_SIGINFO_T)
#
# HAVE_SOCK_T
#
SET(CMAKE_EXTRA_INCLUDE_FILES "sys/socket.h")
CHECK_TYPE_SIZE(socklen_t HAVE_SOCKLEN_T)
IF(HAVE_SOCKLEN_T)
	SET(EPI_HAVE_SOCKLEN_T HAVE_SOCKLEN_T)
ENDIF(HAVE_SOCKLEN_T)
#
# EPI_HAVE_PTHREADS
#
set(THREADS_PREFER_PTHREADS_FLAG ON)
find_package(Threads REQUIRED)
IF(Threads_FOUND)
	SET(EPI_HAVE_PTHREADS Threads_FOUND)
	CHECK_SYMBOL_EXISTS(PTHREAD_MUTEX_INITIALIZER	pthread.h	HAVE_PTHREAD_MUTEX_INITIALIZER)
ENDIF(Threads_FOUND)
#
CHECK_STRUCT_HAS_MEMBER("struct tm" tm_gmtoff	time.h	EPI_HAVE_TM_GMTOFF LANGUAGE C)
#
# Windows-specific checks for MinGW
#
CHECK_STRUCT_HAS_MEMBER("struct ipc_perm" __key "sys/ipc.h;sys/shm.h"   EPI_HAVE_IPC_PERM___KEY LANGUAGE C)
CHECK_STRUCT_HAS_MEMBER("struct ipc_perm"  _key "sys/ipc.h;sys/shm.h"   EPI_HAVE_IPC_PERM__KEY  LANGUAGE C)
CHECK_STRUCT_HAS_MEMBER("struct ipc_perm"   key "sys/ipc.h;sys/shm.h"   EPI_HAVE_IPC_PERM_KEY   LANGUAGE C)

IF(EXISTS /proc/sys/kernel/random/uuid)
	SET(HAVE_PROC_RANDOM_UUID ON)
ENDIF(EXISTS /proc/sys/kernel/random/uuid)
IF(EXISTS /dev/random)
	SET(HAVE_DEV_RANDOM ON)
ENDIF(EXISTS /dev/random)

configure_file(txcoreconfig.h.in ${TEXIS_BINARY_DIR}/txcoreconfig.h)

include_directories(${TEXIS_BINARY_DIR})

add_executable(sizes sizes.c ${TEXIS_BINARY_DIR}/txcoreconfig.h)
target_link_libraries(sizes m)
target_compile_definitions(sizes PRIVATE EPI_SHARED_EXT="${CMAKE_SHARED_LIBRARY_SUFFIX}")

add_custom_command(
	TARGET sizes POST_BUILD
	BYPRODUCTS ${TEXIS_BINARY_DIR}/sizes.h
	COMMAND sizes ${TEXIS_BINARY_DIR}/sizes.h
)

IF(TEST_ATOMIC_FUNCS)
add_executable(atomicTests atomicTests.c)
add_dependencies(atomicTests sizes)
target_link_libraries(atomicTests Threads::Threads)
add_custom_command(
	TARGET atomicTests POST_BUILD
	COMMAND atomicTests threadsafeness
)
ENDIF(TEST_ATOMIC_FUNCS)

add_custom_target(sizes.h)
add_custom_target(txcoreconfig.h)

IF(PARSERS_FOUND)

    # sql1.y's 23 shift/reduce and 38 reduce/reduce conflicts are
    # longstanding and expected; quiet bison's conflict warnings where
    # supported (-Wno-conflicts-* needs bison >= 2.6; macOS /usr/bin/bison
    # is 2.3 and would reject the flags).
    SET(SQL1_BISON_ARGS "")
    IF(BISON_EXECUTABLE)
        EXECUTE_PROCESS(COMMAND ${BISON_EXECUTABLE} --version
                        OUTPUT_VARIABLE _bison_version_out ERROR_QUIET)
        IF(_bison_version_out MATCHES "([0-9]+\\.[0-9]+)")
            IF(NOT CMAKE_MATCH_1 VERSION_LESS 2.6)
                SET(SQL1_BISON_ARGS -Wno-conflicts-sr -Wno-conflicts-rr)
            ENDIF()
        ENDIF()
    ENDIF()

    GENERATE_BISON_FLEX_SOURCES(${TEXISAPI_SOURCE_DIR}/sql1.y "${SQL1_BISON_ARGS}"
       ${TEXISAPI_SOURCE_DIR}/scn1.l   "")

    INCLUDE_DIRECTORIES(${TEXIS_BINARY_DIR})
#	include_directories(${LIBXML2_INCLUDE_DIRS})
#	message(STATUS ${LIBXML2_INCLUDE_DIRS})

  list(APPEND BUILD_SRCS
    #MetaMorph
    api3.c
    api3i.c
    backref.c
    cp.c
    dupapi.c
    eqv.c
    eqvlst.c
    eqvpar.c
    eqvwr.c
    file.c
    fpath.c
    freadex.c
    getmdx.c
    langc.c
    mdpar.c
    mm3e.c
    mmreport.c
    mmsg.c
    npm.c
    npmp.c
    otree.c
    phrase.c
    ppm.c
    presuf.c
    rex.c
    salloc.c
    spm.c
    sysmsg.c
    txstrncpy.c
    unicode.c
    xpm.c
    #Field Math
    abstract.c
    bitfuncs.c
    fldmath.c
    fldops.c
    fldmathp.c
    fldtostr.c
    ftinternal.c
    geocode.c
    inetfuncs.c
    inetutil.c
    json.c
    spectype.c
    udateops.c
    sqlfuncutils.c
    strcmp.c
    strfmt.c
    txstring.c
    urlFuncs.c
    uuid.c
    vxsandr.c
    ###########################################################################
    #Tuple manipulation
    newstats.c
    tup_appe.c
    tup_eval.c
    tup_indx.c
    tup_prod.c
    tup_proj.c
    tup_read.c
    tup_unio.c
    ###########################################################################
    #Data dictionary object
    addindex.c
    addtable.c
    cleanup.c
    convtbl.c
    csystbl.c
    dbsize.c
    ddcache.c
    ddclose.c
    ddgetidx.c
    ddopen.c
    droptbl.c
    encrypt.c
    gettable.c
    mkperms.c
    perms.c
    sysstats.c
    texerr.c
    #WTF    txlicense.c
    #WTF    txlicsvr.c
    updstats.c
    #WTF    vxglobs.o
    ###########################################################################
    #Locking
    locktables.c
    ndblock.c
    nlock.c
    sem.c
    txlock.c
    ###########################################################################
    #Btree
    abtree.c
    btpage.c
    coord.c
    dbidx.c
    fbtree.c
    vbtree.c
    ###########################################################################
    #Parse tree/query maipulation
    coltbl.c
    demux.c
    disppred.c
    distinct.c
    dop.c
    doq.c
    enumparm.c
    groupby.c
    node_buffer.c
    node_hint.c
    node_info.c
    node_join.c
    node_rename.c
    node_table.c
    optpred.c
    orderbyaliases.c
    preptree.c
    procupd.c
    product.c
    prq.c
    queryfld.c
    queue.c
    readtoke.c
    readnode.c
    reorgqn.c
    repreptree.c
    subpred.c
    sufield.c
    treeconv.c
    valtbl.c
    ###########################################################################
    #ODBC API
    catalog.c
    connect.c
    #WTF    convtopc.c
    dreason.c
    execute.c
    mmhlst.c
    prepare.c
    results.c
    #WTF    texnet.c
    tosql.c
    typeconv.c
    ###########################################################################
    #Index Objects
    3dbindex.c
    addtoind.c
    alterIndex.c
    fdbi.c
    fdbim.c
    fheap.c
    fheapwtix.c
    fldcmp.c
    getindex.c
    imanip.c
    index.c
    indexOptions.c
    keyrec.c
    merge.c
    meter.c
    pile.c
    range.c
    rexlex.c
    ripmm.c
    rppm.c
    tin.c
    updindex.c
    xtree.c
    ###########################################################################
    #DBTBL Database Tables
    aufunx.c

    #for vdist used in aufunx.c/sql-userfunc.c - ajf 2025-10-25
    ${CMAKE_SOURCE_DIR}/extern/SimSIMD/c/lib.c
    ${CMAKE_SOURCE_DIR}/src/duktape/globals/vector-distance.c

    # NOTE: vec sources (vecindex.c, vecindex_aux_btree.c,
    # vec_blas_probe.c, plus the usearch HNSW backend lib.cpp) live in
    # VEC_SRCS below, added only to texisapi.  Keeping them out of
    # BUILD_SRCS lets the lean `texisbuild` lib avoid pulling vec code
    # (and the entire faiss/openblas/libgomp/libgfortran chain) into
    # tools that don't need it.

    # SYSUPDATE table maintenance (auto-create, progress reporting,
    # DROP cleanup) for both fulltext and vec index operations.
    sysupdate.c


    blob.c
    blobz.c
    btcache.c
    bubble.c
    bufferdbf.c
    creatdb.c
    dbf.c
    dbtable.c
    dbtbl.c
    dbtemp.c
    dd.c
    fdbf.c
    fld.c
    fldcopy.c
    idxinfo.c
    iextra.c
    indcache.c
    #WTF    jdbf.c
    kdbf.c
    kdbfchk.c
    kdbfutil.c
    kfbtree.c
    nblobs.c
    noopdbf.c
    planpred.c
    predopt.c
    qnode.c
    ramdbf.c
    recid.c
    text2mm.c
    ###########################################################################
    #SQL Parser
    ${BF_SOURCES}
    setprop.c
    strcat3.c
    ###########################################################################
    #Metamorph handling
    ddmmapi.c
    likep.c
    match.c
    mminfo.c
    mmsup.c
    txopencp.c
    ###########################################################################
    # Helpers from EPI
    slist.c
    sregex.c
    ###########################################################################
    # HT Helpers
    cgi.c
    charsets.c
    htbuf.c
    htcharset.c
    #WTF    htformat.c
    #WTF    htparse.c
    htpf.c
    htprintf.c
    monochar.c
    urlprotocols.c
    urlutils.c
    ###########################################################################
    # Miscellaneous
    abend.c
    compactTable.c
    crargv.c
    createLocks.c
    createLocksViaMonitor.c
    datasize.c
    ddtotext.c
    errnoNames.c
    ezsock.c
    ezsockbuf.c
    ftiValueWithCooked.c
    getpid.c
    globalcp.c
    h_errnoNames.c
    incarray.c
    jtreadex.c
    lang.c
    #WTF    libxmlLoader.c
    #WTF    libxsltLoader.c
    logsql.c
    macro.c
    maxrlim.c
    mime.c
    mimetype.c
    #WTF    mimeReader.c
    monservLog.c
    #WTF    monsock.c
    #WTF    ncgvars.c
    nlst.c
    nmmsg.c
    nmmsgaux.c
    os.c
    parsetim.c
    portdump.c
    readconf.c
    ringbuffer.c
    rmcommon.c
    sqlLookup.c
    #WTF    sqlRefInfo.c
    strbuf.c
    strlst.c
    strlwr.c
    strtodat.c
    strtoh.c
    sysdep.c
    syssched.c
    system.c
    systemStatuses.c
    tempfn.c
    texglob.c
    txputmsgflags.c
    trigger.c
    txFhandleEvent.c
    txMutex.c
    txSslSyms.c
    txfree.c
    txpmbuf.c
    txstrcmp.c
    txthreads.c
    #WTF    txtoc.c
    unixcompress.c
    vcalendar.c
    #WTF    vwlist.c
    #WTF    watchPath.c
    #WTF    xmlapi.c
    #WTF    xsltapi.c
    zlibutil.c
  )

  # Texis intentionally keeps its proven old-style C; a few files trip
  # modern gcc analyzers with false positives. Suppress per-file rather
  # than touch the code. All three flags are gcc-only (clang/AppleClang
  # reject them with "unknown warning option"), hence the GNU guard.
  #   ppm.c:172      -Walloc-size-larger-than  (signed/unsigned range analysis)
  #   kdbfchk.c:1770 -Wstringop-overflow       (old-style buffer math)
  #   sregex.c:295   -Wreturn-local-addr       (two-pass compile always
  #                    callocs before returning; local tmp[] cannot escape)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(ppm.c PROPERTIES
        COMPILE_OPTIONS "-Wno-alloc-size-larger-than")
    set_source_files_properties(kdbfchk.c PROPERTIES
        COMPILE_OPTIONS "-Wno-stringop-overflow")
    set_source_files_properties(sregex.c PROPERTIES
        COMPILE_OPTIONS "-Wno-return-local-addr")
  endif()

  # Clang (macOS AppleClang) fires a different set on the same proven
  # old-style C. Guarded because several of these flag names are
  # clang-only and gcc would reject them.
  #   fldops.c/xtree.c    ITEM1/ITEM2 comma-macro scratch vars (-Wunsequenced);
  #                       fldop1.c is #included ~12x by fldops.c (texis
  #                       template-by-inclusion), so the flag must be on
  #                       the fldops.c TU, not on fldop1.c
  #   cp.c                K&R functions falling off end / dangling else
  #   vecindex_ivfpq.cpp  dbstruct.h's empty `union {} extra` in QNODE has
  #                       size 0 in C / 1 in C++; member unused and QNODE
  #                       never crosses the C/C++ boundary
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(xtree.c PROPERTIES
        COMPILE_OPTIONS "-Wno-unsequenced")
    set_source_files_properties(fldops.c PROPERTIES
        COMPILE_OPTIONS "-Wno-switch;-Wno-unsequenced")
    set_source_files_properties(cp.c PROPERTIES
        COMPILE_OPTIONS "-Wno-return-type;-Wno-dangling-else")
    set_source_files_properties(salloc.c PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-value")
    set_source_files_properties(predopt.c treeconv.c disppred.c dreason.c doq.c
        PROPERTIES COMPILE_OPTIONS "-Wno-enum-compare")
    set_source_files_properties(qnode.c lockd_lock.c lockrequest.c
        node_buffer.c PROPERTIES COMPILE_OPTIONS "-Wno-switch")
    set_source_files_properties(htprintf.c PROPERTIES
        COMPILE_OPTIONS "-Wno-string-plus-int")
    set_source_files_properties(abend.c PROPERTIES
        COMPILE_OPTIONS "-Wno-pointer-to-int-cast")
    set_source_files_properties(sysdep.c PROPERTIES
        COMPILE_OPTIONS "-Wno-tautological-constant-compare")
    set_source_files_properties(vecindex_ivfpq.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-extern-c-compat")
  endif()

  if(LOCK_SERVER)
    list(APPEND BUILD_SRCS
        lockclient.c
        lockrequest.c
        lockd_connection.c
        lockd_counter.c
        lockd_launch.c
        lockd_lock.c
        lockd_resource.c
    )
  endif(LOCK_SERVER)

  if(NOT WIN32)
    list(APPEND BUILD_SRCS shell.c)
  endif()

  # Self-describing vector-value header decode/encode (vecvalue.h).
  # Pure byte inspection, no vec/faiss deps -- shared by texisapi AND
  # the lean texisbuild (fldops.c's cell readers call it everywhere).
  list(APPEND BUILD_SRCS vec_value.c)

  # ----- Shared compilation of common (non-vec) texis sources --------
  # An OBJECT library compiles each ${BUILD_SRCS} file once; the
  # resulting .o files are then linked into BOTH static libs below via
  # $<TARGET_OBJECTS:texis_common_objs>.  Before this refactor, every
  # file in BUILD_SRCS compiled twice (once per static lib), which made
  # clean builds noticeably slower and produced byte-identical .o files
  # in two locations.
  add_library(texis_common_objs OBJECT ${BUILD_SRCS})
  target_include_directories(texis_common_objs PUBLIC
      ${TEXISAPI_BINARY_DIR}
      ${CMAKE_SOURCE_DIR}/extern/SimSIMD/include)

  # ----- Vec sources (texisapi only) ---------------------------------
  # Pulled OUT of BUILD_SRCS so they don't end up in texisbuild.
  # texisbuild then has no symbols referencing TXvecIvfpqBackend /
  # faiss::* — lean tools linking against texisbuild won't drag faiss
  # or its libopenblas/libgomp/libgfortran transitive deps into
  # DT_NEEDED.  See architectural notes at top of file.
  set(VEC_SRCS
      vecindex.c
      vecindex_aux_btree.c
      vec_blas_probe.c
      ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/usearch/c/lib.cpp)

  # ----- texisbuild: lean -------------------------------------------
  # Common .o's + dummy aux (which since this refactor reports the
  # vendored snapshot version, NOT a literal "Dummy version" string).
  # No vec, no faiss.  Used by tools that don't run SQL / vec dispatch.
  add_library(texisbuild
    $<TARGET_OBJECTS:texis_common_objs>
    langNgramsDummy.c
    txdummyver.c
  )

  # ----- texisapi: full ---------------------------------------------
  # Common .o's + real build-stamped aux files + vec sources + (when
  # FAISS is built) the IVFPQ backend.  Used by tools that need SQL +
  # vec dispatch (tsql, addtable, kdbfchk) and by rampart-sql.so.
  add_library(texisapi
    $<TARGET_OBJECTS:texis_common_objs>
    ${TEXISAPI_BINARY_DIR}/langNgrams.c
    ${TEXISAPI_SOURCE_DIR}/texisapi.c
    ${TEXISAPI_BINARY_DIR}/texver.c
    ${VEC_SRCS}
  )

  # Add the FAISS IVFPQ backend source only when FAISS is built —
  # i.e., not on 32-bit ARM (see top-of-file RP_NO_FAISS detection).
  # Only texisapi gets it (texisbuild stays faiss-free).
  if(NOT RP_NO_FAISS)
      target_sources(texisapi PRIVATE
          ${TEXISAPI_SOURCE_DIR}/vecindex_ivfpq.cpp)
      if(RP_ARMHF_GCC8_FAISS_WORKAROUND)
          # faiss_ivfpq is built with -D_GLIBCXX_USE_CXX11_ABI=0 on
          # this target (see extern/faiss/CMakeLists.txt).  Match it
          # here so std::string-typed faiss:: symbols (FaissException
          # ctor, etc.) referenced from vecindex_ivfpq.cpp resolve.
          # No other file in libtexisapi.a touches faiss:: directly
          # (they all go through C wrappers), so the ABI flag is
          # scoped to this one source.
          set_source_files_properties(
              ${TEXISAPI_SOURCE_DIR}/vecindex_ivfpq.cpp
              PROPERTIES COMPILE_FLAGS "-D_GLIBCXX_USE_CXX11_ABI=0")
      endif()
  endif()

  string(ASCII 27 ESC)
  set(GREEN "${ESC}[32m")
  set(RESET "${ESC}[0m")

  # Same simsimd flag layout as src/CMakeLists.txt — see the comment block
  # there for the rationale.  Kept in sync by hand.
  set(SIMSIMD_WARN "-Wno-unused-function -Wno-unknown-pragmas -Wno-unused-variable -Wno-unused-but-set-variable")
  set(SIMSIMD_FLAGS "-DSIMSIMD_DYNAMIC_DISPATCH=1")
  set(SIMSIMD_ENABLE_TARGETS "")
  set(SIMSIMD_DISABLE_TARGETS "")

  if(RP_SIMSIMD_APPROXIMATE_SCALAR)
      set(SIMSIMD_FLAGS "${SIMSIMD_FLAGS} -DRP_SIMSIMD_APPROXIMATE_SCALAR=1")
  endif()

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
          set(SIMSIMD_ENABLE_TARGETS "${SIMSIMD_ENABLE_TARGETS} -DSIMSIMD_TARGET_HASWELL=1")
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
              set(SIMSIMD_ENABLE_TARGETS "${SIMSIMD_ENABLE_TARGETS} -DSIMSIMD_TARGET_SKYLAKE=1")
              message("-- ${GREEN}Skylake (AVX512)${RESET}")
          endif()
      else()
          set(SIMSIMD_DISABLE_TARGETS "-DSIMSIMD_TARGET_SKYLAKE=0 -DSIMSIMD_TARGET_ICE=0 -DSIMSIMD_TARGET_GENOA=0 -DSIMSIMD_TARGET_SAPPHIRE=0 -DSIMSIMD_TARGET_TURIN=0")
      endif()

      if(HAVE_HASWELL AND HAVE_SKYLAKE)
          # Test Ice Lake (AVX512VNNI, AVX512VBMI2, AVX512BITALG)
          set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma -mf16c -mavx512f -mavx512dq -mavx512bw -mavx512vl -mavx512vnni -mavx512vbmi2 -mavx512bitalg -mavx512vp2intersect")
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
              set(SIMSIMD_ENABLE_TARGETS "${SIMSIMD_ENABLE_TARGETS} -DSIMSIMD_TARGET_ICE=1")
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
              set(SIMSIMD_ENABLE_TARGETS "${SIMSIMD_ENABLE_TARGETS} -DSIMSIMD_TARGET_SAPPHIRE=1")
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
              set(SIMSIMD_ENABLE_TARGETS "${SIMSIMD_ENABLE_TARGETS} -DSIMSIMD_TARGET_GENOA=1 -DSIMSIMD_TARGET_TURIN=1")
              message("-- ${GREEN}BF16 (Turin/Genoa)${RESET}")
          endif()
      endif()
  endif()

  set(SIMSIMD_FLAGS "${SIMSIMD_FLAGS} ${SIMSIMD_DISABLE_TARGETS}")
  message("-- ${GREEN}Consumer flags: ${SIMSIMD_FLAGS}${RESET}")
  message("-- ${GREEN}lib.c enable flags: ${SIMSIMD_ENABLE_TARGETS}${RESET}")

  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/extern/SimSIMD/c/lib.c
    PROPERTIES
      COMPILE_FLAGS "-O3 ${SIMSIMD_WARN} ${SIMSIMD_FLAGS} ${SIMSIMD_ENABLE_TARGETS}"
  )

  # Consumers route through lib.c's runtime dispatcher via DYNAMIC_DISPATCH=1.
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/duktape/globals/vector-distance.c
    PROPERTIES
      COMPILE_FLAGS "-O3 ${SIMSIMD_WARN} ${SIMSIMD_FLAGS}"
  )

  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/vecindex.c
    PROPERTIES
      COMPILE_FLAGS "${SIMSIMD_WARN} ${SIMSIMD_FLAGS}"
  )

ELSE(PARSERS_FOUND)

    # Opps, didn't find the parsers
    MESSAGE(SEND_ERROR "The parsing tools (yacc/flex) can't be found.")

ENDIF(PARSERS_FOUND)

if(HAVE_RE2)
    target_sources(texisbuild PRIVATE ${TEXIS_SOURCE_DIR}/thirdparty/cre2/src/cre2.cpp)
	target_sources(texisapi PRIVATE ${TEXIS_SOURCE_DIR}/thirdparty/cre2/src/cre2.cpp)

	target_link_libraries(texisbuild PUBLIC re2)
	target_link_libraries(texisapi PUBLIC re2)

    add_dependencies(texisbuild re2 absl::base)
    add_dependencies(texisapi re2 absl::base)

endif(HAVE_RE2)

if(NO_RAM_EQUIVS)
	add_definitions(-DNO_RAM_EQUIVS)
else(NO_RAM_EQUIVS)
	target_sources(texisapi PRIVATE equivs.c)
  target_sources(texisbuild PRIVATE equivs.c)
endif(NO_RAM_EQUIVS)

#
# Always generate the texver.c
#
add_custom_target(always_gen ALL
  COMMAND ${TEXISAPI_SOURCE_DIR}/gensec.sh ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR}
  BYPRODUCTS ${TEXISAPI_BINARY_DIR}/texver.c
)

add_dependencies(texisapi always_gen)

#add_dependencies(texisbuild always_gen)
#
# FTINTERNAL Symbols
# generates ftinternalsyms.h, which is included by everyone
#
file(GLOB FTINTENAL_EXT_SYMS CONFIGURE_DEPENDS extension/ftinternal/*.syms)
add_custom_command(OUTPUT ${TEXIS_BINARY_DIR}/ftinternalsyms.h
  COMMAND ${TEXISAPI_SOURCE_DIR}/genftinternalsyms.sh ${TEXIS_BINARY_DIR}/ftinternalsyms.h ${TEXISAPI_SOURCE_DIR}/ftinternal.syms ${FTINTENAL_EXT_SYMS}
  DEPENDS ftinternal.syms ${FTINTENAL_EXT_SYMS}
)

# DEPENDS keyword required -- without it the path is interpreted as a
# COMMAND to execute, producing "ftinternalsyms.h: not found" when
# anything actually invokes this target.  (Latent before because
# nothing did, and the file was generated as a side effect of being
# listed in genFldFuncsSorted's source list.)
add_custom_target(ftinternalsyms-h DEPENDS ${TEXIS_BINARY_DIR}/ftinternalsyms.h)

# Every .c file that pulls in dd.h transitively needs ftinternalsyms.h
# to be generated first.  Without these add_dependencies the link-time
# targets race against the custom_command under `make -j` and can compile
# dd.h before the header exists, producing
#   "fatal error: ftinternalsyms.h: No such file or directory".
# texis_common_objs is the OBJECT library where the dd.h-including .c
# files actually compile; texisapi + texisbuild aggregate it.  Wiring
# all three ensures every downstream consumer (rampart-sql, tsql,
# texislockd, etc.) blocks on the header until it exists.
add_dependencies(texis_common_objs ftinternalsyms-h)
add_dependencies(texisapi          ftinternalsyms-h)
add_dependencies(texisbuild        ftinternalsyms-h)

#
# Sort fldFuncs.c
#
# NOTE: ftinternalsyms.h is NOT listed as a source here.  Listing it
# creates a second cmake-resolved file dependency that, under -j,
# can race with the texis_common_objs -> ftinternalsyms-h chain and
# fire genftinternalsyms.sh twice concurrently -- the script's `>`
# followed by `>>` is not atomic, so the two runs interleave and the
# file ends up with duplicated content.  Modeling the dep purely via
# the custom_target (add_dependencies just below) gives cmake one
# unique producer path.
add_executable(genFldFuncsSorted fldFuncs.c)
add_dependencies(genFldFuncsSorted ftinternalsyms-h)
add_custom_command(TARGET genFldFuncsSorted POST_BUILD
		BYPRODUCTS ${TEXISAPI_BINARY_DIR}/fldFuncsSorted.c
		COMMAND genFldFuncsSorted ${TEXISAPI_BINARY_DIR}/fldFuncsSorted.c
)
add_dependencies(genFldFuncsSorted ${JANSSON_PREFIX} )
add_dependencies(texisapi genFldFuncsSorted)
add_dependencies(texisbuild genFldFuncsSorted)

#
# Generate monochar.c and monochar.h from the mappings-monobyte directory
#

file(GLOB MONOCHAR_MAPS CONFIGURE_DEPENDS mappings-monobyte/*.txt)
add_custom_command(OUTPUT monochar.c
    COMMAND ${TEXISAPI_SOURCE_DIR}/genmonochar.sh ${TEXISAPI_SOURCE_DIR} ${TEXISAPI_BINARY_DIR}/monochar.c ${TEXISAPI_BINARY_DIR}/monochar.h
    DEPENDS ${MONOCHAR_MAPS} ${TEXISAPI_SOURCE_DIR}/genmonochar.sh ${TEXISAPI_SOURCE_DIR}/procmap
    BYPRODUCTS ${TEXISAPI_BINARY_DIR}/monochar.h
)

#langNgrams
file(GLOB LANG_TRAIN CONFIGURE_DEPENDS langTrainData/*.txt)
add_executable(genNgrams genNgrams.c)
add_custom_command(OUTPUT ${TEXISAPI_BINARY_DIR}/langNgrams.c
  COMMAND test -e ${TEXISAPI_BINARY_DIR}/langNgrams.c || ${TEXISAPI_SOURCE_DIR}/genNgrams.sh ${TEXIS_SOURCE_DIR} ${TEXISAPI_BINARY_DIR}/langNgrams.c ${LANG_TRAIN}
  WORKING_DIRECTORY ${TEXISAPI_BINARY_DIR}
  DEPENDS ${LANG_TRAIN} ${TEXISAPI_SOURCE_DIR}/genNgrams.sh genNgrams
)


if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
target_link_libraries(genNgrams LINK_PUBLIC texisbuild kvm Threads::Threads)
else()
target_link_libraries(genNgrams LINK_PUBLIC texisbuild Threads::Threads)
endif()


#
# Anyone else in this project linking to texisapi needs to include generated files
# and link with jansson and zlib
#
target_include_directories(
  texisbuild PUBLIC
  ${TEXISAPI_BINARY_DIR}
  # ajf - 2025-10-25
  ${CMAKE_SOURCE_DIR}/extern/SimSIMD/include
  # usearch (vec-index backend)
  ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/usearch/include
  ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/usearch/c
  # FAISS (vec-index backend, IVFPQ — phase 1 wiring)
  ${CMAKE_SOURCE_DIR}/extern/faiss
)

target_link_options(texisbuild PUBLIC -L${JANSSON_RELEASE_DIR})

# texisbuild is lean — no vec sources, so it has no symbol references
# to faiss::* and doesn't need faiss_ivfpq linked.  Skipping faiss here
# is what keeps libopenblas/libgomp/libgfortran out of texisbuild
# consumers' DT_NEEDED.
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_link_libraries(texisbuild LINK_PUBLIC m z kvm Threads::Threads ${JANSSON_LIBS})
else()
    target_link_libraries(texisbuild LINK_PUBLIC m z Threads::Threads ${JANSSON_LIBS})
endif()

#
# Anyone else in this project linking to texisapi needs to include generated files
# and link with jansson and zlib
#
target_include_directories(
  texisapi PUBLIC
  ${TEXISAPI_BINARY_DIR}
  # ajf - 2025-10-25
  ${CMAKE_SOURCE_DIR}/extern/SimSIMD/include
  # usearch (vec-index backend)
  ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/usearch/include
  ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/usearch/c
  # FAISS (vec-index backend, IVFPQ — phase 1 wiring)
  ${CMAKE_SOURCE_DIR}/extern/faiss
)
target_link_options(texisapi PUBLIC -L${JANSSON_RELEASE_DIR})
# FAISS_IVFPQ must be force-loaded so that .o files defining only weak
# C++ typeinfo (e.g. FaissException.cpp.o) are pulled into the consuming
# shared object.  Without this, rampart-sql.so fails to load with
# "undefined symbol: _ZTIN5faiss14FaissExceptionE" because the linker
# drops the .o whose symbols are only referenced weakly by external
# try/catch blocks.  GNU ld and Apple ld spell this differently.
#
# Note: this PUBLIC propagation drags libopenblas/libgomp/libgfortran
# into every texisapi consumer's DT_NEEDED.  For consumers that DON'T
# need vec dispatch (texislockd specifically) there's a separate
# override in extern/texis/apps/CMakeLists.txt that links libtexisapi.a
# via $<TARGET_FILE:...> file-ref instead of the target, dropping the
# PUBLIC interface to keep DT_NEEDED minimal.
if(RP_NO_FAISS)
    target_link_libraries(texisapi PUBLIC m z ${JANSSON_LIBS})
else()
    # faiss_ivfpq is propagated as a *plain* target dep — NOT
    # whole-archive'd.  Plain on-demand archive semantics means consumers
    # only pull faiss .o files for symbols their code actually
    # references; tools like addtable / kdbfchk / texislockd that
    # never call into vec dispatch don't end up with faiss code in
    # their binary (and so don't drag libopenblas / libgomp /
    # libgfortran into DT_NEEDED).
    #
    # rampart-sql.so DOES need --whole-archive — without it the linker
    # drops FaissException.cpp.o whose `_ZTIN5faiss14FaissExceptionE`
    # typeinfo is only weakly referenced by external try/catch.  That
    # whole-archive directive lives at the rampart-sql.so link site
    # (FAISS_IVFPQ_LINK in src/CMakeLists.txt) rather than here.
    target_link_libraries(texisapi PUBLIC m z ${JANSSON_LIBS}
        faiss_ivfpq)
endif()

#target_include_directories(PUBLIC, $(TEXISAPI_SOURCE_DIR))
#
# Install Rules
#

#if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
#install(TARGETS texisapi
#ARCHIVE DESTINATION lib
#)
#else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
#  install(TARGETS texisapi
#  ARCHIVE
#  )
#endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

set(TEXISAPI_H_FILES
  ${TEXIS_BINARY_DIR}/sizes.h
  ${TEXIS_SOURCE_DIR}/include/txsql.h
  ${TEXISAPI_SOURCE_DIR}/texisapi_struct.h
  ${TEXIS_SOURCE_DIR}/include/texis_countinfo.h
  ${TEXISAPI_SOURCE_DIR}/texisapi_funcs.h
)
add_custom_command(OUTPUT ${TEXIS_BINARY_DIR}/texisapi.h
  COMMAND cat ${TEXISAPI_H_FILES} > ${TEXIS_BINARY_DIR}/texisapi.h
  DEPENDS ${TEXISAPI_H_FILES}
)
target_sources(genFldFuncsSorted PRIVATE ${TEXIS_BINARY_DIR}/texisapi.h)

# texisapi.h is included by nearly every texis TU (dbquery.h:33), but the
# only build edge to its generator used to be genFldFuncsSorted's
# target_sources above.  texisapi/texisbuild are covered transitively
# (they depend on genFldFuncsSorted), but texis_common_objs — an OBJECT
# library, which does NOT inherit those target-level deps — could start
# compiling before texisapi.h existed ("texisapi.h file not found" on the
# first `make -j` pass; seen on macOS 2026-07-25 where genFldFuncsSorted
# was delayed behind the jansson ExternalProject).  Same race class and
# same fix pattern as ftinternalsyms-h above.
add_custom_target(texisapi-h DEPENDS ${TEXIS_BINARY_DIR}/texisapi.h)
add_dependencies(texis_common_objs texisapi-h)

# Two more edges of the same race, all hit when compiling common objs on a
# fresh tree with high -j:
#   texint.h -> lockrequest.h -> jansson.h  (jansson ExternalProject's
#     install tree; only genFldFuncsSorted had this dep)
#   dbtbl.c:4068 #include "fldFuncsSorted.c"  (POST_BUILD product of
#     genFldFuncsSorted)
# genFldFuncsSorted transitively implies jansson + texisapi.h, but keep
# the explicit edges: they document real include-level deps and survive
# any future reshuffle of genFldFuncsSorted's own deps.
# (genNgrams needs the same headers but is already ordered by its
# texisbuild link dependency.  No cycle here: genFldFuncsSorted builds
# from fldFuncs.c alone and links no texis lib.)
add_dependencies(texis_common_objs ${JANSSON_PREFIX})
add_dependencies(texis_common_objs genFldFuncsSorted)

#install(FILES ${TEXISAPI_BINARY_DIR}/ftinternalsyms.h;${TEXIS_BINARY_DIR}/texisapi.h DESTINATION include/texis)

if(TEXIS_SILENCE_AUDITED_WARNINGS)
    target_compile_options(texisbuild PRIVATE ${TEXIS_AUDITED_WARNING_FLAGS})
    target_compile_options(texisapi   PRIVATE ${TEXIS_AUDITED_WARNING_FLAGS})
endif()

if(SILENCE_VENDORED_WARNINGS)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/cre2/src/cre2.cpp
        ${CMAKE_SOURCE_DIR}/extern/texis/thirdparty/usearch/c/lib.cpp
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()
