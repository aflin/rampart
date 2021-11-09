# find_library(EVENT_LIBRARY event)
if(MSVC)
  add_compile_options(${TARGET_NAME}  /W4 /WX)
else()
  add_compile_options(${TARGET_NAME}  -Wall -Wextra -Werror)
endif()
if(EVENT_LIBRARY)
add_executable(texislockd
  lockd_connection.c
  lockd_counter.c
  lockd_lock.c
  lockd_main.c
  lockd_resource.c
)
target_link_libraries(texislockd LINK_PUBLIC event)
target_link_libraries(texislockd LINK_PUBLIC texisapi mmapi3 z)
set(LOCKD_EXE  ${CMAKE_CURRENT_BINARY_DIR}/texislockd PARENT_SCOPE)

if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS texislockd RUNTIME DESTINATION bin)
else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS texislockd RUNTIME)
endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

endif(EVENT_LIBRARY)
