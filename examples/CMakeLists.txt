set(EXAMPLE_FILES
	"server.js"
	"servermod.js"
	"mPurpose"
	"tetris-tutorial"
)

# Copying to the src dir is temporary. In the future
# a new directory under build called 'examples' will be created.
foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
file(COPY ${EXAMPLE_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()