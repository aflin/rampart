
set(CMAKE_BUILD_TYPE Debug)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${SRC})

enable_testing()
# example test
add_executable(simple_test simple_test.c)
add_test(NAME simple COMMAND simple_test)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/duktape/modules/utils)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/duktape/core/module)

set(TEST_FILES
	"ramis.js"
	"crypto.js"
	"dbfunc.js"
)

# Copying to the src dir is temporary. In the future
# a new directory under build called 'test' will be created.
foreach(TEST_FILE ${TEST_FILES})
file(COPY ${TEST_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()
