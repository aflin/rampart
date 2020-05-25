if(WANT_SSL)
	set(OPENSSL_PREFIX openssl-1.1.1g)

	find_program(MAKE_EXE NAMES gmake nmake make)
	ExternalProject_Add(${OPENSSL_PREFIX}
		PREFIX ${OPENSSL_PREFIX}
		URL https://www.openssl.org/source/openssl-1.1.1g.tar.gz
		CONFIGURE_COMMAND <SOURCE_DIR>/config shared --api=1.0.0 enable-weak-ssl-ciphers enable-ssl2 enable-ssl3 enable-ssl3-method --prefix=${CMAKE_CURRENT_BINARY_DIR}/${OPENSSL_PREFIX}
		BUILD_COMMAND ${MAKE_EXE}
		INSTALL_COMMAND ${MAKE_EXE} install_sw
		INSTALL_DIR ${OPENSSL_PREFIX}
		LOG_INSTALL 1
	)

	# get the unpacked source directory path
	ExternalProject_Get_Property(${OPENSSL_PREFIX} SOURCE_DIR)
	message(STATUS "Source directory of ${OPENSSL_PREFIX} ${SOURCE_DIR}")

	# set the include directory variable and include it
	set(OPENSSL_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${OPENSSL_PREFIX}/lib)
	set(OPENSSL_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/${OPENSSL_PREFIX}/include)
	include_directories(${OPENSSL_INCLUDE_DIRS})

	link_directories(${OPENSSL_RELEASE_DIR})
	set(OPENSSL_LIBS crypto ssl)
	set(OPENSSL_LIBRARY_DIRS ${OPENSSL_RELEASE_DIR})

	#
	# Install Rules
	#
	#install(
	#		DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${OPENSSL_PREFIX}/include
	#		DESTINATION include/texis
	#)
	#install(
	#		DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${OPENSSL_PREFIX}/include
	#		DESTINATION include/texis
	#)

	# verify that the OPENSSL header files can be included
	set(CMAKE_REQUIRED_INCLUDES_SAVE ${CMAKE_REQUIRED_INCLUDES})
	set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES} 	${OPENSSL_INCLUDE_DIRS})
	check_include_file("openssl/ssl.h" HAVE_OPENSSL)
	set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES_SAVE})
	 if (NOT HAVE_OPENSSL)
		message(STATUS "Did not build OPENSSL correctly as cannot find openssl/ssl.h. Will build it.")
		set(HAVE_OPENSSL 1)
	endif (NOT HAVE_OPENSSL)
endif(WANT_SSL)
