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
struct evhtp_ws_frame_hdr_s {
    uint8_t opcode : 4,
            rsv3   : 1,
            rsv2   : 1,
            rsv1   : 1,
            fin    : 1;

    #define OP_CONT          0x0
    #define OP_TEXT          0x1
    #define OP_BIN           0x2
    #define OP_NCONTROL_RES1 0x3
    #define OP_NCONTROL_RES2 0x4
    #define OP_NCONTROL_RES3 0x5
    #define OP_NCONTROL_RES4 0x6
    #define OP_NCONTROL_RES5 0x7
    #define OP_CLOSE         0x8
    #define OP_PING          0x9
    #define OP_PONG          0xA
    #define OP_CONTROL_RES1  0xB
    #define OP_CONTROL_RES2  0xC
    #define OP_CONTROL_RES3  0xD
    #define OP_CONTROL_RES4  0xE
    #define OP_CONTROL_RES5  0xF

    uint8_t len  : 7,
            mask : 1;
} __attribute__((packed));
//struct evhtp_ws_frame_hdr_s;
typedef struct evhtp_ws_frame_hdr_s evhtp_ws_frame_hdr;

struct evhtp_ws_frame_s {
    evhtp_ws_frame_hdr hdr;

    uint32_t masking_key;
    uint64_t payload_len;
    char     payload[];
};

typedef enum evhtp_ws_parser_state evhtp_ws_parser_state;
typedef struct evhtp_ws_frame_s     evhtp_ws_frame;

enum evhtp_ws_parser_state {
    ws_s_start = 0,
    ws_s_fin_rsv_opcode,
    ws_s_mask_payload_len,
    ws_s_ext_payload_len_16,
    ws_s_ext_payload_len_64,
    ws_s_masking_key,
    ws_s_payload
};

struct evhtp_ws_parser_s {
    evhtp_ws_parser_state state;
    uint64_t              content_len;
    uint64_t              orig_content_len;
    uint64_t              content_idx;
    uint16_t              status_code;
    void                * usrdata;
    evhtp_ws_frame        frame;
    struct event        * pingev;
    uint8_t               pingct;
};

//struct evhtp_ws_parser_s;
struct evhtp_ws_frame_s;
struct evhtp_ws_data_s;

//defined in evhtp.h
//typedef struct evhtp_ws_parser_s    evhtp_ws_parser;
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

EVHTP_EXPORT evhtp_ws_data   * evhtp_ws_data_new(const char * data, size_t len, uint8_t opcode);
EVHTP_EXPORT unsigned char   * evhtp_ws_data_pack(evhtp_ws_data * ws_data, size_t * out_len);

#endif

