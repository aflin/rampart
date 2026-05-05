set(JANSSON_PREFIX jansson-2.12)

set(JANSSON_CMAKE_ARGS
	-DJANSSON_BUILD_DOCS=OFF
	-DJANSSON_EXAMPLES=OFF
	-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)
if(WIN32)
	list(APPEND JANSSON_CMAKE_ARGS -DJANSSON_WITHOUT_TESTS=ON)
endif()

ExternalProject_Add(${JANSSON_PREFIX}
	PREFIX ${JANSSON_PREFIX}
	SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${JANSSON_PREFIX}
	INSTALL_DIR contrib/${JANSSON_PREFIX}
	CMAKE_ARGS ${JANSSON_CMAKE_ARGS}
		-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
		-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
	LOG_INSTALL 1
)

# get the unpacked source directory path
ExternalProject_Get_Property(${JANSSON_PREFIX} SOURCE_DIR)
message(STATUS "Source directory of ${JANSSON_PREFIX} ${SOURCE_DIR}")

# set the include directory variable and include it
set(JANSSON_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/contrib/${JANSSON_PREFIX}/lib)
set(JANSSON_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/contrib/${JANSSON_PREFIX}/include)
include_directories(${JANSSON_INCLUDE_DIRS})

# Use the full archive path rather than -L + -ljansson.  The release
# dir is created by jansson's ExternalProject_Add at build time, so
# during a parallel build any target that links before jansson finishes
# would emit a "search path not found" ld warning under link_directories.
# Linking by file path lets CMake track existence as a real dependency.
set(JANSSON_LIBS ${JANSSON_RELEASE_DIR}/libjansson.a)
set(JANSSON_LIBRARY_DIRS ${JANSSON_RELEASE_DIR})
