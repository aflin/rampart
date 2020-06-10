set(EXTERN_DIR ${CMAKE_SOURCE_DIR}/extern)

find_package(Git QUIET)
if(GIT_FOUND)
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

include(${EXTERN_DIR}/openssl.cmake)
include(${EXTERN_DIR}/oniguruma.cmake)
include(${EXTERN_DIR}/curl.cmake)
include_directories(${ONIGURUMA_TARGET_DIR}/include)
include(${EXTERN_DIR}/libevent.cmake)
include(${EXTERN_DIR}/libevhtp.cmake)