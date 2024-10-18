file(GLOB TEXSRCS RELATIVE ${TEXIS_SOURCE_DIR}/docs ${CMAKE_CURRENT_SOURCE_DIR}/*.tex*)
file(GLOB STYSRCS RELATIVE ${TEXIS_SOURCE_DIR}/docs ${CMAKE_CURRENT_SOURCE_DIR}/*.sty*)
set(LATEXSRCS)

foreach(texfile IN LISTS TEXSRCS)
  string(REGEX REPLACE "\\.in$" "" realtexfile ${texfile})
  string(COMPARE EQUAL ${texfile} ${realtexfile} NoSubst)
  if(${NoSubst})
    configure_file(${texfile} ${TEXIS_BINARY_DIR}/docs/${realtexfile} COPYONLY)
  else(${NoSubst})
    configure_file(${texfile} ${TEXIS_BINARY_DIR}/docs/${realtexfile} @ONLY)
  endif(${NoSubst})
  set(LATEXSRCS ${LATEXSRCS} ${TEXIS_BINARY_DIR}/docs/${realtexfile})
endforeach()
foreach(styfile IN LISTS STYSRCS)
  string(REGEX REPLACE "\\.in$" "" realstyfile ${styfile})
  string(COMPARE EQUAL ${styfile} ${realstyfile} NoSubst)
  if(${NoSubst})
    configure_file(${styfile} ${TEXIS_BINARY_DIR}/docs/${realstyfile} COPYONLY)
  else(${NoSubst})
    configure_file(${styfile} ${TEXIS_BINARY_DIR}/docs/${realstyfile} @ONLY)
  endif(${NoSubst})
  set(LATEXSRCS ${LATEXSRCS} ${TEXIS_BINARY_DIR}/docs/${realstyfile})
endforeach()

configure_file(${TEXIS_SOURCE_DIR}/docs/mklatex.sh.in ${TEXIS_BINARY_DIR}/docs/mklatex.sh)

add_custom_target(Docs ALL DEPENDS texis.pdf)

add_custom_command(OUTPUT texis.pdf
  COMMAND ${TEXIS_BINARY_DIR}/docs/mklatex.sh ARGS texis.tex
  DEPENDS ${LATEXSRCS}
)
