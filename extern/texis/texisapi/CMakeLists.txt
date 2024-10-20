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
CHECK_INCLUDE_FILE("windows.h"  HAVE_WINDOWS_H)

IF(HAVE_WINDOWS_H)
  set(WINDOWS_INCLUDE_FILE "windows.h")
  set(CMAKE_EXTRA_INCLUDE_FILES ${WINDOWS_INCLUDE_FILE})
ENDIF(HAVE_WINDOWS_H)


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
CHECK_TYPE_SIZE("__int64" __INT64_T)
IF(HAVE___INT64_T)
		SET(EPI_HAVE___INT64 HAVE___INT64_T)
ENDIF(HAVE___INT64_T)
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

    GENERATE_BISON_FLEX_SOURCES(${TEXISAPI_SOURCE_DIR}/sql1.y ""
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
    shell.c
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

  add_library(texisbuild
    ${BUILD_SRCS}
    langNgramsDummy.c
    txdummyver.c
  )

  add_library(texisapi
    ${BUILD_SRCS}
    ${TEXISAPI_BINARY_DIR}/langNgrams.c
    ${TEXISAPI_SOURCE_DIR}/texisapi.c
    ${TEXISAPI_BINARY_DIR}/texver.c
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

add_custom_target(ftinternalsyms-h ${TEXIS_BINARY_DIR}/ftinternalsyms.h)

#
# Sort fldFuncs.c
#
add_executable(genFldFuncsSorted fldFuncs.c ${TEXIS_BINARY_DIR}/ftinternalsyms.h)
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
  DEPENDS ${LANG_TRAIN} ${TEXISAPI_SOURCE_DIR}/genNgrams.sh genNgrams
)


if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
target_link_libraries(genNgrams LINK_PUBLIC texisbuild z kvm Threads::Threads)
else()
target_link_libraries(genNgrams LINK_PUBLIC texisbuild z Threads::Threads)
endif()


#
# Anyone else in this project linking to texisapi needs to include generated files
# and link with jansson and zlib
#
target_include_directories(texisbuild PUBLIC ${TEXISAPI_BINARY_DIR})
target_link_options(texisbuild PUBLIC -L${JANSSON_RELEASE_DIR})

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_link_libraries(texisbuild LINK_PUBLIC m z kvm Threads::Threads ${JANSSON_LIBS})
else()
    target_link_libraries(texisbuild LINK_PUBLIC m z Threads::Threads ${JANSSON_LIBS})
endif()

#
# Anyone else in this project linking to texisapi needs to include generated files
# and link with jansson and zlib
#
target_include_directories(texisapi PUBLIC ${TEXISAPI_BINARY_DIR})
target_link_options(texisapi PUBLIC -L${JANSSON_RELEASE_DIR})
target_link_libraries(texisapi PUBLIC m z ${JANSSON_LIBS})

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

#set(TEXISAPI_H_FILES
#  ${TEXIS_BINARY_DIR}/sizes.h
#  ${TEXIS_SOURCE_DIR}/include/txsql.h
#  ${TEXISAPI_SOURCE_DIR}/texisapi_struct.h
#  ${TEXIS_SOURCE_DIR}/include/texis_countinfo.h
#  ${TEXISAPI_SOURCE_DIR}/texisapi_funcs.h
#)
#add_custom_command(OUTPUT ${TEXIS_BINARY_DIR}/texisapi.h
#  COMMAND cat ${TEXISAPI_H_FILES} > ${TEXIS_BINARY_DIR}/texisapi.h
#  DEPENDS ${TEXISAPI_H_FILES}
#)
#target_sources(genFldFuncsSorted PRIVATE ${TEXIS_BINARY_DIR}/texisapi.h)

#install(FILES ${TEXISAPI_BINARY_DIR}/ftinternalsyms.h;${TEXIS_BINARY_DIR}/texisapi.h DESTINATION include/texis)
