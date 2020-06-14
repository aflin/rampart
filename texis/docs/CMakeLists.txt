file(GLOB TEXSRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.tex*)
file(GLOB STYSRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.sty*)
set(LATEXSRCS)

foreach(texfile IN LISTS TEXSRCS)
  string(REGEX REPLACE "\\.in$" "" realtexfile ${texfile})
  string(COMPARE EQUAL ${texfile} ${realtexfile} NoSubst)
  if(${NoSubst})
    configure_file(${texfile} ${CMAKE_CURRENT_BINARY_DIR}/${realtexfile} COPYONLY)
  else(${NoSubst})
    configure_file(${texfile} ${CMAKE_CURRENT_BINARY_DIR}/${realtexfile} @ONLY)
  endif(${NoSubst})
  set(LATEXSRCS ${LATEXSRCS} ${CMAKE_CURRENT_BINARY_DIR}/${realtexfile})
endforeach()
foreach(styfile IN LISTS STYSRCS)
  string(REGEX REPLACE "\\.in$" "" realstyfile ${styfile})
  string(COMPARE EQUAL ${styfile} ${realstyfile} NoSubst)
  if(${NoSubst})
    configure_file(${styfile} ${CMAKE_CURRENT_BINARY_DIR}/${realstyfile} COPYONLY)
  else(${NoSubst})
    configure_file(${styfile} ${CMAKE_CURRENT_BINARY_DIR}/${realstyfile} @ONLY)
  endif(${NoSubst})
  set(LATEXSRCS ${LATEXSRCS} ${CMAKE_CURRENT_BINARY_DIR}/${realstyfile})
endforeach()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mklatex.sh.in ${CMAKE_CURRENT_BINARY_DIR}/mklatex.sh)

add_custom_target(Docs ALL DEPENDS texis.pdf)

add_custom_command(OUTPUT texis.pdf
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/mklatex.sh ARGS texis.tex
  DEPENDS ${LATEXSRCS}
)
