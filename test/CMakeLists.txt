set(TEST_FILES
	"utils.js"
	"ramis.js"
	"crypto.js"
)

# Copying to the src dir is temporary. In the future
# a new directory under build called 'test' will be created.
foreach(TEST_FILE ${TEST_FILES})
file(COPY ${TEST_FILE} DESTINATION ${CMAKE_BINARY_DIR}/src)
endforeach()
