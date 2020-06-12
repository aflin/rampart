INCLUDE(${CMAKE_SOURCE_DIR}/cmake/FindParsers.cmake)
#include(FindLibXml2)

IF(PARSERS_FOUND)

    GENERATE_BISON_FLEX_SOURCES(${CMAKE_CURRENT_SOURCE_DIR}/sql1.y ""
       ${CMAKE_CURRENT_SOURCE_DIR}/scn1.l   "")

    INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
#	include_directories(${LIBXML2_INCLUDE_DIRS})
#	message(STATUS ${LIBXML2_INCLUDE_DIRS})

	add_library(texisapi
	STATIC
###########################################################################
#Field Math
		abstract.c
		bitfuncs.c
		fldmath.c
		fldops.c
		fldmathp.c
		fldtostr.c
		ftinternal.c
		geocode.c
		inetfuncs.c
    inetutil.c
		json.c
		spectype.c
		shell.c
    sqlfuncutils.c
		strcmp.c
		strfmt.c
		txstring.c
		urlFuncs.c
		vxsandr.c
###########################################################################
#Tuple manipulation
		newstats.c
		tup_appe.c
		tup_eval.c
		tup_indx.c
		tup_prod.c
		tup_proj.c
		tup_read.c
		tup_unio.c
###########################################################################
#Data dictionary object
		addindex.c
		addtable.c
		cleanup.c
		convtbl.c
		csystbl.c
		dbsize.c
		ddcache.c
		ddclose.c
		ddgetidx.c
		ddopen.c
		droptbl.c
		encrypt.c
		gettable.c
		mkperms.c
		perms.c
		sysstats.c
		texerr.c
#WTF		txlicense.c
#WTF		txlicsvr.c
		updstats.c
#WTF		vxglobs.o
###########################################################################
#Locking
		ndblock.c
		nlock.c
		sem.c
		txlock.c
###########################################################################
#Btree
		abtree.c
		btpage.c
		coord.c
		dbidx.c
		fbtree.c
		vbtree.c
###########################################################################
#Parse tree/query maipulation
		coltbl.c
		demux.c
		disppred.c
		distinct.c
		dop.c
		doq.c
		enumparm.c
		groupby.c
		node_hint.c
		node_join.c
		node_rename.c
		node_table.c
		optpred.c
		preptree.c
		procupd.c
		product.c
		prq.c
		queryfld.c
		queue.c
		readtoke.c
		readnode.c
		reorgqn.c
		repreptree.c
		subpred.c
		sufield.c
		treeconv.c
		valtbl.c
###########################################################################
#ODBC API
		catalog.c
		connect.c
#WTF		convtopc.c
		dreason.c
		execute.c
		mmhlst.c
		prepare.c
		results.c
#WTF		texnet.c
		tosql.c
		typeconv.c
###########################################################################
#Index Objects
		3dbindex.c
		addtoind.c
		alterIndex.c
		fdbi.c
		fdbim.c
		fheap.c
		fheapwtix.c
		fldcmp.c
		getindex.c
		imanip.c
		index.c
		indexOptions.c
		keyrec.c
		merge.c
		meter.c
		pile.c
		range.c
		rexlex.c
		ripmm.c
		rppm.c
		tin.c
		updindex.c
		xtree.c
###########################################################################
#DBTBL Database Tables
		aufunx.c
		blob.c
		blobz.c
		btcache.c
		bubble.c
		creatdb.c
		dbf.c
		dbtable.c
		dbtbl.c
		dbtemp.c
		dd.c
		fdbf.c
		fld.c
		fldcopy.c
		idxinfo.c
		iextra.c
		indcache.c
#WTF		jdbf.c
		kdbf.c
		kdbfutil.c
		kfbtree.c
		nblobs.c
		noopdbf.c
		planpred.c
		predopt.c
		qnode.c
		ramdbf.c
		recid.c
		text2mm.c
###########################################################################
#SQL Parser
		${BF_SOURCES}
		setprop.c
		strcat3.c
###########################################################################
#Metamorph handling
		ddmmapi.c
		likep.c
		match.c
		mminfo.c
		mmsup.c
		txopencp.c
###########################################################################
# Helpers from EPI
		slist.c
		sregex.c
###########################################################################
# HT Helpers
    cgi.c
    charsets.c
		htbuf.c
    htcharset.c
#WTF		htformat.c
#WTF		htparse.c
    htpf.c
		htprintf.c
    monochar.c
    urlprotocols.c
    urlutils.c
###########################################################################
# Miscellaneous
		abend.c
		compactTable.c
		crargv.c
    createLocks.c
		createLocksViaMonitor.c
		datasize.c
		ddtotext.c
		errnoNames.c
    ezsock.c
		ftiValueWithCooked.c
		getpid.c
		h_errnoNames.c
    incarray.c
		jtreadex.c
		lang.c
		langNgramsDummy.c
#WTF		libxmlLoader.c
#WTF		libxsltLoader.c
		logsql.c
    macro.c
		maxrlim.c
    mime.c
    mimetype.c
#WTF		mimeReader.c
		monservLog.c
#WTF		monsock.c
#WTF		ncgvars.c
    nlst.c
    nmmsg.c
    nmmsgaux.c
    os.c
		parsetim.c
		portdump.c
		readconf.c
		rmcommon.c
		sqlLookup.c
#WTF		sqlRefInfo.c
		strbuf.c
    strlst.c
		strlwr.c
		strtodat.c
		strtoh.c
		sysdep.c
		syssched.c
		system.c
		systemStatuses.c
		tempfn.c
		texglob.c
		texver.c
    txputmsgflags.c
		trigger.c
		txFhandleEvent.c
		txMutex.c
		txSslSyms.c
		txfree.c
		txpmbuf.c
		txstrcmp.c
		txthreads.c
#WTF		txtoc.c
		unixcompress.c
		vcalendar.c
#WTF		vwlist.c
#WTF		watchPath.c
#WTF		xmlapi.c
#WTF		xsltapi.c
		zlibutil.c
###########################################################################
    texisapi.c
	)
ELSE(PARSERS_FOUND)
    # Opps, didn't find the parsers
    MESSAGE(SEND_ERROR "The parsing tools (yacc/flex) can't be found.")
ENDIF(PARSERS_FOUND)

#
# Always generate the texver.c
#
add_custom_target(texver.c ALL
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gensec.sh ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR}
    )
add_custom_target(always_gen ALL
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gensec.sh ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR}
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/texver.c
)
add_dependencies(texisapi always_gen)
#
# Sort fldFuncs.c
#
add_executable(genFldFuncsSorted fldFuncs.c ftinternalsyms.h)
add_custom_command(TARGET genFldFuncsSorted POST_BUILD
		BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/fldFuncsSorted.c
		COMMAND genFldFuncsSorted ${CMAKE_CURRENT_BINARY_DIR}/fldFuncsSorted.c
)
add_dependencies(texisapi genFldFuncsSorted)
#
# FTINTERNAL Symbols
# generates ftinternalsyms.h, which is included by everyone
#
file(GLOB FTINTENAL_EXT_SYMS CONFIGURE_DEPENDS extension/ftinternal/*.syms)
add_custom_command(OUTPUT ftinternalsyms.h
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/genftinternalsyms.sh ${CMAKE_CURRENT_BINARY_DIR}/ftinternalsyms.h ${CMAKE_CURRENT_SOURCE_DIR}/ftinternal.syms ${FTINTENAL_EXT_SYMS}
  DEPENDS ftinternal.syms ${FTINTENAL_EXT_SYMS}
)
#add_dependencies(genFldFuncsSorted ftinternalsyms.h)
#
# Generate monochar.c and monochar.h from the mappings-monobyte directory
#
file(GLOB MONOCHAR_MAPS CONFIGURE_DEPENDS mappings-monobyte/*.txt)
add_custom_command(OUTPUT monochar.c
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/genmonochar.sh ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/monochar.c ${CMAKE_CURRENT_BINARY_DIR}/monochar.h
    DEPENDS ${MONOCHAR_MAPS} ${CMAKE_CURRENT_SOURCE_DIR}/genmonochar.sh ${CMAKE_CURRENT_SOURCE_DIR}/procmap
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/monochar.h
)

#
# Anyone else in this project linking to texisapi needs to include generated files
# and link with jansson, mmapi3 and zlib
#
target_include_directories(texisapi PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_options(texisapi PUBLIC -L${JANSSON_RELEASE_DIR})
target_link_libraries(texisapi LINK_PUBLIC mmapi3 z ${JANSSON_LIBS})
#target_include_directories(mmapi3, PUBLIC, $(CMAKE_CURRENT_SOURCE_DIR))
#
# Install Rules
#
if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
install(TARGETS texisapi
ARCHIVE DESTINATION lib
)
else(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)
  install(TARGETS texisapi
  ARCHIVE
  )
endif(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_LESS 3.14)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ftinternalsyms.h DESTINATION include/texis)
