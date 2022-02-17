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

add_executable(rex ${CMAKE_SOURCE_DIR}/texisapi/rex.c)
target_compile_definitions(rex PRIVATE TEST=1)
set(REX_EXE  ${CMAKE_CURRENT_BINARY_DIR}/rex PARENT_SCOPE)

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
set(TSQL_EXE  ${CMAKE_CURRENT_BINARY_DIR}/tsql PARENT_SCOPE)

target_link_libraries(rex LINK_PUBLIC texisapi)
target_link_libraries(metamorph LINK_PUBLIC texisapi)
target_link_libraries(backref LINK_PUBLIC texisapi)
target_link_libraries(tsql PUBLIC Threads::Threads)
target_link_libraries(tsql LINK_PUBLIC texisapi z)
target_link_libraries(addtable PUBLIC Threads::Threads)
target_link_libraries(addtable LINK_PUBLIC texisapi z)
target_link_libraries(kdbfchk PUBLIC Threads::Threads)
target_link_libraries(kdbfchk LINK_PUBLIC texisapi z)

if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS rex tsql RUNTIME DESTINATION bin)
else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS rex tsql RUNTIME)
endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

if(LOCK_SERVER AND EVENT_LIBRARY)
add_executable(texislockd
  lockd_main.c
)

if(EPI_HAS_EVENT_LOCATION)
target_link_libraries(texislockd LINK_PUBLIC
        ${EPI_BINARY_EVENT_DIR}/lib/libevent_core.a
        ${EPI_BINARY_EVENT_DIR}/lib/libevent_pthreads.a 
)
else(EPI_HAS_EVENT_LOCATION)
target_link_libraries(texislockd LINK_PUBLIC event)
endif(EPI_HAS_EVENT_LOCATION)

target_link_libraries(texislockd LINK_PUBLIC texisapi z)
set(LOCKD_EXE  ${CMAKE_CURRENT_BINARY_DIR}/texislockd PARENT_SCOPE)

if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS texislockd RUNTIME DESTINATION bin)
else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS texislockd RUNTIME)
endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

endif(LOCK_SERVER AND EVENT_LIBRARY)
