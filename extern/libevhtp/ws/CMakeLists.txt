set (evhtp_ws_SRCS
	sha1.c
	base.c
	evhtp_ws.c
)

add_target_sources(libevhtp ${evhtp_ws_SRCS})

install(FILES evhtp_ws.h DESTINATION ${INCLUDE_INSTALL_DIR}/evhtp2/ws)
