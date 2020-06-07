set(GAWK_PREFIX gawk)

find_program(AUTORECONF NAMES autoreconf)
find_program(MAKE_EXE NAMES gmake nmake make)
ExternalProject_Add(${GAWK_PREFIX}
	PREFIX ${GAWK_PREFIX}
  GIT_REPOSITORY https://git.savannah.gnu.org/git/gawk.git
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/${GAWK_PREFIX}
  BUILD_COMMAND ${MAKE_EXE}
)
ExternalProject_Add_Step(${GAWK_PREFIX} pre_configure 
  DEPENDEES download
  DEPENDERS configure
  COMMAND ${AUTORECONF} -vfi <SOURCE_DIR>/configure.ac
)


ExternalProject_Get_Property(${GAWK_PREFIX} SOURCE_DIR)
message(STATUS "Source directory of ${GAWK_PREFIX} ${SOURCE_DIR}")

set(GAWK_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${GAWK_PREFIX}/lib)
set(GAWK_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${GAWK_PREFIX}/include)

include_directories(${GAWK_INCLUDE_DIRS})
link_directories(${GAWK_RELEASE_DIR})

