add_executable(utils 
        utils.c 
	${SRC}/duktape/core/duktape.c
	${SRC}/duktape/core/module.c
	${SRC}/duktape/globals/misc.c
)
target_link_libraries(utils LINK_PUBLIC 
        m dl pthread
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/helloworld.txt ${CMAKE_CURRENT_BINARY_DIR}/helloworld.txt COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/loremipsum.txt ${CMAKE_CURRENT_BINARY_DIR}/loremipsum.txt COPYONLY)
add_test(NAME utils COMMAND utils)
