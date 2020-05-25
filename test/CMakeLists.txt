file(GLOB TSQLTESTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/sql ${CMAKE_CURRENT_SOURCE_DIR}/sql/*.sql*)
file(GLOB TSQLTESTSARGS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/sql ${CMAKE_CURRENT_SOURCE_DIR}/sql/*.args)
file(GLOB EXPECTEDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/expected ${CMAKE_CURRENT_SOURCE_DIR}/expected/*)

#
# Most tests can be copied, along with args
#
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

add_test(NAME apitest1 COMMAND apitest1 ${CMAKE_CURRENT_BINARY_DIR}/junk "SELECT * FROM SYSTABLES")

set(THREADS_PREFER_PTHREADS_FLAG ON)
find_package(Threads REQUIRED)

add_executable(apitest1
apitest1.c)
target_link_libraries(apitest1 PUBLIC Threads::Threads)
target_link_libraries(apitest1 LINK_PUBLIC texisapi mmapi3 z)
