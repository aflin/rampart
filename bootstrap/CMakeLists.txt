include (CheckCSourceCompiles)
include (CheckCSourceRuns)
include(CheckIncludeFile)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(CheckTypeSize)
include(FindThreads)
include(WriteCompilerDetectionHeader)

#
# How many args does getpwuid take?
#
include(${CMAKE_SOURCE_DIR}/cmake/CheckHasGetPWUID.cmake)
if(HAVE_GETPWUID_5)
	SET(EPI_HAVE_GETPWUID_R_INT 1)
endif (HAVE_GETPWUID_5)
if(HAVE_GETPWUID_4)
	SET(EPI_HAVE_GETPWUID_R_PASSWD 1)
endif(HAVE_GETPWUID_4)

#
# Compiler compiler settings for c++
#
write_compiler_detection_header(
FILE ${CMAKE_BINARY_DIR}/txcoreconfig_gen.h
PREFIX TX
COMPILERS GNU Clang MSVC Intel
FEATURES cxx_alignof
)
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
#
CHECK_C_SOURCE_COMPILES("main(){int x = alignof(x);}" EPI_HAVE_ALIGNOF)
CHECK_C_SOURCE_COMPILES("main(){int x = _alignof(x);}" EPI_HAVE__ALIGNOF)
CHECK_C_SOURCE_COMPILES("main(){int x = __alignof(x);}" EPI_HAVE___ALIGNOF)
CHECK_C_SOURCE_COMPILES("main(){int x = __alignof__(x);}" EPI_HAVE___ALIGNOF__)
CHECK_C_SOURCE_COMPILES("main(){int x = 5, res; res = __atomic_fetch_add(&x, 3, __ATOMIC_SEQ_CST); }" EPI_HAVE_ATOMIC_FETCH_FUNCTIONS)
CHECK_C_SOURCE_COMPILES("main(){int x = 5, res; res = __sync_fetch_and_add(&x, 3); }" EPI_HAVE_SYNC_FETCH_FUNCTIONS)
IF(EPI_HAVE_UNISTD_H)
	set(CMAKE_EXTRA_INCLUDE_FILES ${CMAKE_EXTRA_INCLUDE_FILES} "unistd.h")
ENDIF(EPI_HAVE_UNISTD_H)
CHECK_C_SOURCE_RUNS("#include <fcntl.h>\nmain(){int fd; fd = open(\"${CMAKE_SOURCE_DIR}/CMakeLists.txt\", O_RDONLY); if(fd<0) return 2; fd = open(\"${CMAKE_SOURCE_DIR}/cmakelists.txt\", O_RDONLY); if(fd<0) return 0; return 1;}" FS_CASE_INSENSITIVE)
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
CHECK_SYMBOL_EXISTS(qsort_r		stdlib.h		EPI_HAVE_QSORT_R)
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
CHECK_TYPE_SIZE("__float128" __FLOAT128)
IF(HAVE___FLOAT128)
		SET(EPI_HAVE___FLOAT128 EPI_HAVE___FLOAT128)
ENDIF(HAVE___FLOAT128)
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

configure_file(txcoreconfig.h.in ${CMAKE_BINARY_DIR}/txcoreconfig.h)

include_directories(${CMAKE_BINARY_DIR})

add_executable(sizes sizes.c)
target_compile_definitions(sizes PRIVATE EPI_SHARED_EXT="${CMAKE_SHARED_LIBRARY_SUFFIX}")

add_custom_command(
	TARGET sizes POST_BUILD
	BYPRODUCTS ${CMAKE_BINARY_DIR}/sizes.h
	COMMAND sizes ${CMAKE_BINARY_DIR}/sizes.h
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
#add_custom_target(${CMAKE_BINARY_DIR}/txconfig.h)

# target_link_libraries(metamorph LINK_PUBLIC mmapi3)
#
# Install Rules
#
install(FILES ${CMAKE_BINARY_DIR}/txcoreconfig.h ${CMAKE_BINARY_DIR}/txcoreconfig_gen.h ${CMAKE_BINARY_DIR}/sizes.h
	TYPE INCLUDE
)
