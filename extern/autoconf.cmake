macro(autoreconf PREFIX DIR)
  # find_program(AUTORECONF_EXE NAMES autoreconf)
  # if(NOT ${AUTORECONF_EXE})
  #   message(FATAL_ERROR "autoreconf not found")
  # endif()
  if(NOT "${${PREFIX}_AUTORECONF_RUN}")
    execute_process(
      COMMAND autoreconf -vfi
      WORKING_DIRECTORY ${DIR}
      RESULT_VARIABLE ${PREFIX}_AUTORECONF_RESULT
    )
    message(STATUS "autoreconf result: ${${PREFIX}_AUTORECONF_RESULT}")
    if(${PREFIX}_AUTORECONF_RESULT EQUAL "0")
      option(${PREFIX}_AUTORECONF_RUN "whether autoreconf has run for ${PREFIX}" ON)
    else()
      message(FATAL_ERROR "calling autoreconf failed in ${DIR}")
    endif()
  endif()
endmacro()
