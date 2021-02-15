set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)

add_subdirectory(${EXTERN_DIR}/openssl)

add_subdirectory(${EXTERN_DIR}/oniguruma)

add_subdirectory(${EXTERN_DIR}/curl)

include_directories(${CMAKE_BINARY_DIR}/extern/oniguruma/include)

add_subdirectory(${EXTERN_DIR}/libevent)

add_subdirectory(${EXTERN_DIR}/libevhtp)

add_subdirectory(${EXTERN_DIR}/tidy-html5)

if (NOT CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.0)

include(cmake/ExternalProject.cmake)
ExternalProject_Add( robotstxt
	PREFIX robotstxt
	SOURCE_DIR "${EXTERN_DIR}/robotstxt"
	BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/extern/robotstxt" 
	INSTALL_COMMAND cmake -E echo "Skipping install step."
)

endif()