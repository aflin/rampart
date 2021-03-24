file(GLOB TSQLTESTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/sql ${CMAKE_CURRENT_SOURCE_DIR}/sql/*.sql*)
file(GLOB TSQLTESTSARGS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/sql ${CMAKE_CURRENT_SOURCE_DIR}/sql/*.args)
file(GLOB EXPECTEDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/expected ${CMAKE_CURRENT_SOURCE_DIR}/expected/*)

#
# Most tests can be copied, along with args
#
if(LOCK_SERVER AND EVENT_LIBRARY)
  add_test(NAME Prepare COMMAND ${CMAKE_CURRENT_BINARY_DIR}/pretests.sh $<TARGET_FILE:tsql> ${LOCKD_EXE})
else(LOCK_SERVER and EVENT_LIBRARY)
  add_test(NAME Prepare COMMAND ${CMAKE_CURRENT_BINARY_DIR}/pretests.sh $<TARGET_FILE:tsql>)
endif(LOCK_SERVER AND EVENT_LIBRARY)

foreach(test IN LISTS TSQLTESTS)
  string(REGEX REPLACE "\\.in$" "" testsql ${test})
  string(COMPARE EQUAL ${test} ${testsql} NoSubst)
  if(${NoSubst})
    configure_file(sql/${test} ${CMAKE_CURRENT_BINARY_DIR}/sql/${test} COPYONLY)
  else(${NoSubst})
    configure_file(sql/${test} ${CMAKE_CURRENT_BINARY_DIR}/sql/${testsql} @ONLY)
  endif(${NoSubst})
  add_test(NAME ${testsql} COMMAND runtests.sh ${testsql} $<TARGET_FILE:tsql> $<TARGET_FILE:rex>
  )
endforeach()
foreach(args IN LISTS TSQLTESTSARGS)
  configure_file(sql/${args} ${CMAKE_CURRENT_BINARY_DIR}/sql/${args} COPYONLY)
endforeach()

#
# Copy expected, with substitutions as needed
#
foreach(test IN LISTS EXPECTEDS)
  string(REGEX REPLACE "\\.in$" "" testsql ${test})
  string(COMPARE EQUAL ${test} ${testsql} NoSubst)
  if(${NoSubst})
    configure_file(expected/${test} ${CMAKE_CURRENT_BINARY_DIR}/expected/${testsql} COPYONLY)
  else(${NoSubst})
    configure_file(expected/${test} ${CMAKE_CURRENT_BINARY_DIR}/expected/${testsql} @ONLY)
  endif(${NoSubst})
endforeach()
#
# Copy the standard text files
#
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/text DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
#
# substitutions on the test driver script
#
configure_file(runtests.sh.in runtests.sh @ONLY)
configure_file(pretests.sh.in pretests.sh @ONLY)
configure_file(CTestCustom.cmake.in CTestCustom.cmake)

add_test(NAME apitest1 COMMAND apitest1 ${CMAKE_CURRENT_BINARY_DIR}/junk "SELECT * FROM SYSTABLES")
add_test(NAME "locktest1-1-proc" COMMAND locktest1 ${CMAKE_CURRENT_BINARY_DIR}/junk 1)
add_test(NAME "locktest1-2-proc" COMMAND locktest1 ${CMAKE_CURRENT_BINARY_DIR}/junk 2)
add_test(NAME "locktest1-4-proc" COMMAND locktest1 ${CMAKE_CURRENT_BINARY_DIR}/junk 4)
add_test(NAME "locktest1-8-proc" COMMAND locktest1 ${CMAKE_CURRENT_BINARY_DIR}/junk 8)
add_test(NAME "ringbuffer" COMMAND rbtest)

set(THREADS_PREFER_PTHREADS_FLAG ON)
find_package(Threads REQUIRED)

add_executable(rbtest
rbtest.c)
target_link_libraries(rbtest PUBLIC Threads::Threads)
target_link_libraries(rbtest LINK_PUBLIC texisapi mmapi3 z)

add_executable(apitest1
apitest1.c)
target_link_libraries(apitest1 PUBLIC Threads::Threads)
target_link_libraries(apitest1 LINK_PUBLIC texisapi mmapi3 z)

add_executable(locktest1
locktest1.c)
target_link_libraries(locktest1 PUBLIC Threads::Threads)
target_link_libraries(locktest1 LINK_PUBLIC texisapi mmapi3 z)
