set(LIBEVHTP_SOURCE_DIR ${EXTERN_DIR}/libevhtp)
set(LIBEVHTP_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/libevhtp)

add_subdirectory(${EXTERN_DIR}/libevhtp)

# if(NOT EXISTS ${LIBEVHTP_TARGET_DIR})
#   make_directory(${LIBEVHTP_TARGET_DIR})
# endif()

# execute_process(
#   COMMAND cmake ${LIBEVHTP_SOURCE_DIR} -DCMAKE_PROJECT_libevhtp_INCLUDE=${EXTERN_DIR}/libevhtp-fix.cmake -DINCLUDE_DIR:STRING=${CMAKE_BINARY_DIR}/oniguruma/include -DLIB_DIR:STRING=${CMAKE_BINARY_DIR}/oniguruma/lib
#   WORKING_DIRECTORY ${LIBEVHTP_TARGET_DIR}
#   RESULT_VARIABLE CMAKE_RESULT
# )
# if(NOT CMAKE_RESULT EQUAL "0")
#   message(FATAL_ERROR "cmake failed at ${LIBEVHTP_SOURCE_DIR}")
# endif()

# execute_process(
#   COMMAND make
#   WORKING_DIRECTORY ${LIBEVHTP_TARGET_DIR}
#   RESULT_VARIABLE MAKE_RESULT
# )
# # cmake differs from autoconf in that it must be built in the target directory
# if(NOT MAKE_RESULT EQUAL "0")
#   message(FATAL_ERROR "make failed in ${LIBEVHTP_TARGET_DIR}")
# endif()