add_executable(module module.c 
	${SRC}/duktape/core/duktape.c
	${SRC}/duktape/core/module.c
)
target_link_libraries(module LINK_PUBLIC 
        m dl pthread
)

file(COPY commonjs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME module COMMAND module)