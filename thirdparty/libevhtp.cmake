set(LIBEVHTP_PREFIX libevhtp)
find_program(MAKE_EXE NAMES gmake nmake make)

ExternalProject_Add(${LIBEVHTP_PREFIX}
PREFIX ${LIBEVHTP_PREFIX}
GIT_REPOSITORY https://github.com/criticalstack/libevhtp.git
GIT_TAG 1.2.18
BUILD_COMMAND ${MAKE_EXE}
CMAKE_ARGS -DCMAKE_PROJECT_libevhtp_INCLUDE=${CMAKE_SOURCE_DIR}/thirdparty/libevhtp-fix.cmake -DINCLUDE_DIR:STRING=${ONIGURUMA_INCLUDE_DIRS} -DLIB_DIR:STRING=${ONIGURUMA_RELEASE_DIR}
)

add_dependencies(libevhtp oniguruma)
ExternalProject_Get_Property(${LIBEVHTP_PREFIX} SOURCE_DIR)
ExternalProject_Get_Property(${LIBEVHTP_PREFIX} BINARY_DIR)

message(STATUS "Source directory of ${LIBEVHTP_PREFIX} ${SOURCE_DIR}")
message(STATUS "Build directory of ${LIBEVHTP_PREFIX} ${BINARY_DIR}")

set(LIBEVHTP_RELEASE_DIR ${BINARY_DIR})
set(LIBEVHTP_INCLUDE_DIRS ${BINARY_DIR}/include)