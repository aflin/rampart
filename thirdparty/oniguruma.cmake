set(ONIGURUMA_PREFIX oniguruma)

find_program(AUTORECONF NAMES autoreconf)
find_program(MAKE_EXE NAMES gmake nmake make)
ExternalProject_Add(${ONIGURUMA_PREFIX}
	PREFIX ${ONIGURUMA_PREFIX}
  GIT_REPOSITORY https://github.com/kkos/oniguruma.git
  GIT_TAG v6.9.5_rev1
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --enable-posix-api --prefix=${CMAKE_CURRENT_BINARY_DIR}/${ONIGURUMA_PREFIX}
  BUILD_COMMAND ${MAKE_EXE}
)

ExternalProject_Add_Step(${ONIGURUMA_PREFIX} pre_configure 
  DEPENDEES download
  DEPENDERS configure
  COMMAND ${AUTORECONF} -vfi <SOURCE_DIR>/configure.ac
)

ExternalProject_Get_Property(${ONIGURUMA_PREFIX} SOURCE_DIR)
message(STATUS "Source directory of ${ONIGURUMA_PREFIX} ${SOURCE_DIR}")

set(ONIGURUMA_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${ONIGURUMA_PREFIX}/lib)
set(ONIGURUMA_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${ONIGURUMA_PREFIX}/include)

include_directories(${ONIGURUMA_INCLUDE_DIRS})
link_directories(${ONIGURUMA_RELEASE_DIR})

