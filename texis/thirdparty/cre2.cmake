set(CRE2_PREFIX cre2)

find_program(MAKE_EXE NAMES gmake nmake make)
ExternalProject_Add(${CRE2_PREFIX}
	PREFIX ${CRE2_PREFIX}
	SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${CRE2_PREFIX}
	CONFIGURE_COMMAND <SOURCE_DIR>/configure.sh
	BUILD_COMMAND ${MAKE_EXE}
	INSTALL_COMMAND ${MAKE_EXE} install
	INSTALL_DIR ${CRE2_PREFIX}
	LOG_INSTALL 1
)

# get the unpacked source directory path
ExternalProject_Get_Property(${CRE2_PREFIX} SOURCE_DIR)
message(STATUS "Source directory of ${CRE2_PREFIX} ${SOURCE_DIR}")

# set the include directory variable and include it
set(CRE2_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${CRE2_PREFIX}/lib)
set(CRE2_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${CRE2_PREFIX}/include)
include_directories(${CRE2_INCLUDE_DIRS})

link_directories(${CRE2_RELEASE_DIR})
set(CRE2_LIBS cre2)
set(CRE2_LIBRARY_DIRS ${CRE2_RELEASE_DIR})

#
# Install Rules
#
#install(
#		DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CRE2_PREFIX}/include
#		DESTINATION include/texis
#)
#install(
#		DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CRE2_PREFIX}/include
#		DESTINATION include/texis
#)

# verify that the CRE2 header files can be included
set(CMAKE_REQUIRED_INCLUDES_SAVE ${CMAKE_REQUIRED_INCLUDES})
set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES} 	${CRE2_INCLUDE_DIRS})
check_include_file("cre2/cre2.h" HAVE_CRE2)
set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES_SAVE})
 if (NOT HAVE_CRE2)
	message(STATUS "Did not build CRE2 correctly as cannot find cre2/cre2.h. Will build it.")
	set(HAVE_CRE2 1)
endif (NOT HAVE_CRE2)
