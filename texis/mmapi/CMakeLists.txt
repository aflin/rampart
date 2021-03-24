add_definitions(-DFORMMAPI -DEPI_NO_VERSION_FUNCTIONS)
add_library(mmapi3
	api3.c
	api3i.c
	backref.c
	cp.c
	dupapi.c
	eqv.c
	eqvlst.c
	eqvpar.c
	eqvwr.c
	file.c
	fpath.c
	freadex.c
	getmdx.c
	langc.c
	mdpar.c
	mm3e.c
	mmreport.c
	mmsg.c
	npm.c
	npmp.c
	otree.c
	phrase.c
	ppm.c
	presuf.c
	rex.c
	salloc.c
	spm.c
	sysmsg.c
	txstrncpy.c
	unicode.c
	xpm.c
)

if(HAVE_RE2)
	target_sources(mmapi3 PRIVATE ${CMAKE_SOURCE_DIR}/thirdparty/cre2/src/cre2.cpp)
	target_link_libraries(mmapi3 PUBLIC re2)
endif(HAVE_RE2)
if(NO_RAM_EQUIVS)
	add_definitions(-DNO_RAM_EQUIVS)
else(NO_RAM_EQUIVS)
	target_sources(mmapi3 PRIVATE equivs.c)
endif(NO_RAM_EQUIVS)

#target_include_directories(mmapi3, PUBLIC, $(CMAKE_CURRENT_SOURCE_DIR))
#
# Install Rules
#
if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
	install(TARGETS mmapi3
	ARCHIVE DESTINATION lib
	)
else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
install(TARGETS mmapi3
ARCHIVE
)
endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
