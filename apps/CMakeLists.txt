find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(mmapi3 PUBLIC ${MATH_LIBRARY})
endif()
find_library(DYNAMIC_LINK_LIBRARY dl)
if(DYNAMIC_LINK_LIBRARY)
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
#

add_executable(rex rex.c)
target_compile_definitions(rex PRIVATE TEST=1)
set(REX_EXE  ${CMAKE_CURRENT_BINARY_DIR}/rex PARENT_SCOPE)

add_executable(metamorph metamorph.c)
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

target_link_libraries(rex LINK_PUBLIC mmapi3)
target_link_libraries(metamorph LINK_PUBLIC mmapi3)
target_link_libraries(tsql PUBLIC Threads::Threads)
target_link_libraries(tsql LINK_PUBLIC texisapi mmapi3 z)

install(TARGETS rex tsql RUNTIME)
