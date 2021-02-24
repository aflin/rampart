#ifndef __EVHTP_WS_H__
#define __EVHTP_WS_H__

/**
 * @brief attempt to find the sec-webSocket-key from the input headers,
 *       append the magic string to it, sha1 encode it, then base64 encode
 *       into the output header "sec-websocket-accept"
 *
 * @param hdrs_in
 * @param hdrs_out
 *
 * @return 0 on success, -1 on error
 */

struct evhtp_ws_parser_s;
struct evhtp_ws_frame_s;
struct evhtp_ws_frame_hdr_s;
struct evhtp_ws_data_s;

typedef struct evhtp_ws_parser_s    evhtp_ws_parser;
typedef struct evhtp_ws_frame_s     evhtp_ws_frame;
typedef struct evhtp_ws_frame_hdr_s evhtp_ws_frame_hdr;
typedef struct evhtp_ws_data_s      evhtp_ws_data;
typedef struct evhtp_ws_hooks_s     evhtp_ws_hooks;

typedef int (*evhtp_ws_parser_hook)(evhtp_ws_parser *);
typedef int (*evhtp_ws_parser_data_hook)(evhtp_ws_parser *, const char *, size_t);

struct evhtp_ws_hooks_s {
    evhtp_ws_parser_hook      on_msg_start;
    evhtp_ws_parser_data_hook on_msg_data;
    evhtp_ws_parser_hook      on_msg_fini;
};

EVHTP_EXPORT evhtp_ws_parser * evhtp_ws_parser_new(void);
EVHTP_EXPORT int               evhtp_ws_gen_handshake(evhtp_kvs_t * hdrs_in, evhtp_kvs_t * hdrs_out);
EVHTP_EXPORT ssize_t           evhtp_ws_parser_run(evhtp_ws_parser * p, evhtp_ws_hooks * hooks, const char * data, size_t len);
EVHTP_EXPORT void              evhtp_ws_parser_set_userdata(evhtp_ws_parser * p, void * usrdata);
EVHTP_EXPORT void            * evhtp_ws_parser_get_userdata(evhtp_ws_parser * p);

EVHTP_EXPORT evhtp_ws_data   * evhtp_ws_data_new(const char * data, size_t len);
EVHTP_EXPORT unsigned char   * evhtp_ws_data_pack(evhtp_ws_data * ws_data, size_t * out_len);

#endif

