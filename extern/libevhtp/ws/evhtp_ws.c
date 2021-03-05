/*
 * Most of the websocket code comes from this unfinished fork:
 *
 *   https://github.com/zerotao/libevhtp/tree/libevhtp2
 *
 * It was reintegrated into libevhtp by the authors of Rampart
 * with sooooo much gratitude for not having to do this from scratch
 *    -ajf
 *
 * Changes and additions, Copyright (c) 2021 Aaron Flin and released
 * under the MIT License.
 *
 * ----------------------------------------------------------------------------
 *
 * While this looks nothing like the original code, my initial point of
 * reference was from Marcin Kelar's parser. His license is included here.
 *
 * Marcin originally had his code under the GPL license, I kept seeing his code
 * referenced in other projects, but could not find the original (more
 * specifically, the original license). I ended up finding his email
 * address and asked if I could use it with a less restrictive license.
 * He responded immediately and said yes, and he did! That's an awesome
 * example of the OSS world. Thank you very much Mr. Kelar.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2012-2014 Marcin Kelar
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * The original API can be found here:
 * https://github.com/OrionExplorer/c-websocket
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

#include "../include/internal.h"
#include "../include/evhtp.h"
#include "base.h"
#include "sha1.h"
#include "evhtp_ws.h"

#define EVHTP_WS_MAGIC       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define EVHTP_WS_MAGIC_SZ    36
#define PARSER_STACK_MAX     8192


struct evhtp_ws_data_s {
    evhtp_ws_frame_hdr hdr;
    char               payload[0];
};




static uint8_t _fext_len[129] = {
    [0]   = 0,
    [126] = 2,
    [127] = 8
};


#define MIN_READ(a, b)                    ((a) < (b) ? (a) : (b))
#define HAS_EXTENDED_PAYLOAD_HDR(__frame) ((__frame)->len >= 126)
#define EXTENDED_PAYLOAD_HDR_LEN(__sz) \
    ((__sz >= 126) ? ((__sz == 126) ? 16 : 64) : 0)


static uint32_t __MASK[] = {
    0x000000ff,
    0x0000ff00,
    0x00ff0000,
    0xff000000
};

static uint32_t __SHIFT[] = {
    0, 8, 16, 24
};

void htp__request_free_(evhtp_request_t * request);

ssize_t
evhtp_ws_parser_run(evhtp_ws_parser * p, evhtp_ws_hooks * hooks,
                    const char * data, size_t len) {
    uint8_t      byte;
    char         c;
    size_t       i;
    const char * p_start;
    const char * p_end;
    uint64_t     to_read;
    if (!hooks) {
        return (ssize_t)len;
    }

    for (i = 0; i < len; i++) {
        int res;

        byte = (uint8_t)data[i];
        switch (p->state) {
            case ws_s_start:
                memset(&p->frame, 0, sizeof(p->frame));

                p->state            = ws_s_fin_rsv_opcode;
                p->content_len      = 0;
                p->orig_content_len = 0;
                p->content_idx      = 0;
                p->status_code      = 0;

                if (hooks->on_msg_start) {
                    if ((hooks->on_msg_start)(p)) {
                        return i;
                    }
                }
            /* fall-through */
            case ws_s_fin_rsv_opcode:
                p->frame.hdr.fin    = (byte & 0x1);
                p->frame.hdr.opcode = (byte & 0xF);
                p->state = ws_s_mask_payload_len;
                break;
            case ws_s_mask_payload_len:
                p->frame.hdr.mask   = ((byte & 0x80) ? 1 : 0);
                p->frame.hdr.len    = (byte & 0x7F);

                switch (EXTENDED_PAYLOAD_HDR_LEN(p->frame.hdr.len)) {
                    case 0:
                        p->frame.payload_len = p->frame.hdr.len;
                        p->content_len       = p->frame.payload_len;
                        p->orig_content_len  = p->content_len;


                        if (p->frame.hdr.mask == 1) {
                            p->state = ws_s_masking_key;
                            break;
                        }

                        p->state = ws_s_payload;
                        break;
                    case 16:
                        p->state = ws_s_ext_payload_len_16;
                        break;
                    case 64:
                        p->state = ws_s_ext_payload_len_64;
                        break;
                    default:
                        return -1;
                } /* switch */

                break;
            case ws_s_ext_payload_len_16:
                if (MIN_READ((const char *)(data + len) - &data[i], 2) < 2) {
                    return i;
                }

                p->frame.payload_len = ntohs(*(uint16_t *)&data[i]);
                p->content_len       = p->frame.payload_len;
                p->orig_content_len  = p->content_len;

                /* we only increment 1 instead of 2 since this byte counts as 1 */
                i += 1;

                if (p->frame.hdr.mask == 1) {
                    p->state = ws_s_masking_key;
                    break;
                }

                p->state = ws_s_payload;

                break;
            case ws_s_ext_payload_len_64:
                if (MIN_READ((const char *)(data + len) - &data[i], 8) < 8) {
                    return i;
                }


                p->frame.payload_len = ntohl(*(uint64_t *)&data[i]);
                p->content_len       = p->frame.payload_len;
                p->orig_content_len  = p->content_len;

                /* we only increment by 7, since this byte counts as 1 (total 8
                 * bytes.
                 */
                i += 7;

                if (p->frame.hdr.mask == 1) {
                    p->state = ws_s_masking_key;;
                    break;
                }

                p->state = ws_s_payload;
                break;
            case ws_s_masking_key:
            {
                int min= MIN_READ((const char *)(data + len) - &data[i], 4);
                if (min < 4) 
                {
                    return i;
                }
                p->frame.masking_key = *(uint32_t *)&data[i];
                i       += 3;
                p->state = ws_s_payload;
                if(min==4) // i==len, so go directly to finish.
                    goto fini;
                break;
            }
            case ws_s_payload:
                /* XXX we need to abstract out the masking shit here, so I don't
                 * have a OP_CLOSE type mask function AND a normal data mask
                 * function all in one case.
                 */
                if (p->frame.hdr.opcode == OP_CLOSE && p->status_code == 0) {
                    uint64_t index;
                    uint32_t mkey;
                    int      j1;
                    int      j2;
                    int      m1;
                    int      m2;
                    char     buf[2];

                    /* webosckes will even mask the 2 byte OP_CLOSE portion,
                     * this is a bit hacky, I need to clean this up.
                     */

                    if (MIN_READ((const char *)(data + len) - &data[i], 2) < 2) {
                        return i;
                    }

                    index           = p->content_idx;
                    mkey            = p->frame.masking_key;

                    /* our mod4 for the current index */
                    j1              = index % 4;
                    /* our mod4 for one past the index. */
                    j2              = (index + 1) % 4;

                    /* the masks we will be using to xor the buffers */
                    m1              = (mkey & __MASK[j1]) >> __SHIFT[j1];
                    m2              = (mkey & __MASK[j2]) >> __SHIFT[j2];

                    buf[0]          = data[i] ^ m1;
                    buf[1]          = data[i + 1] ^ m2;

                    /* even though websockets doesn't do network byte order
                     * anywhere else, for some reason, we do it here! NOW
                     * AWESOME!
                     */
                    p->status_code  = ntohs(*(uint16_t *)buf);

                    p->content_len -= 2;
                    p->content_idx += 2;

                    i += 1;

                    /* RFC states that there could be a message after the
                     * OP_CLOSE 2 byte header, so just drop down and attempt
                     * to parse it.
                     */
                }

                p_start = &data[i];
                p_end   = (const char *)(data + len);
                to_read = MIN_READ(p_end - p_start, p->content_len);
                if (to_read > 0) {
                    int  z;
                    char buf[to_read];

                    for (z = 0; z < to_read; z++) {
                        int           j = p->content_idx % 4;
                        unsigned char xformed_oct;

                        xformed_oct     = (p->frame.masking_key & __MASK[j]) >> __SHIFT[j];
                        buf[z]          = (unsigned char)p_start[z] ^ xformed_oct;

                        p->content_idx += 1;
                    }

                    if (hooks->on_msg_data) {
                        if ((hooks->on_msg_data)(p, buf, to_read)) {
                            return i;
                        }
                    }

                    p->content_len -= to_read;
                    i += to_read - 1;
                }
        fini:
                if (p->content_len == 0 && (p->frame.hdr.fin == 1 || p->frame.hdr.opcode & 0x8) ){
                    if (hooks->on_msg_fini) {
                        if ((hooks->on_msg_fini)(p)) {
                            return i;
                        }
                    }
                    p->state = ws_s_start;
                }

                break;
        } /* switch */
    }

    return i;
}         /* evhtp_ws_parser_run */

int
evhtp_ws_gen_handshake(evhtp_kvs_t * hdrs_in, evhtp_kvs_t * hdrs_out) {
    const char * ws_key;
    const char * upgrade;
    char       * magic_w_ws_key;
    size_t       magic_w_ws_key_len;
    size_t       ws_key_len;
    sha1_ctx     sha;
    char       * out        = NULL;
    size_t       out_bytes  = 0;
    char         digest[20] = { 0 };

    if (!hdrs_in || !hdrs_out) {
        return -1;
    }

    if (!(ws_key = evhtp_kv_find(hdrs_in, "sec-webSocket-key"))) {
        return -1;
    }

    if ((ws_key_len = strlen(ws_key)) == 0) {
        return -1;
    }

    magic_w_ws_key_len = EVHTP_WS_MAGIC_SZ + ws_key_len + 1;

    if (!(magic_w_ws_key = calloc(magic_w_ws_key_len, 1))) {
        return -1;
    }

    memcpy(magic_w_ws_key, ws_key, ws_key_len);
    memcpy((void *)(magic_w_ws_key + ws_key_len),
           EVHTP_WS_MAGIC, EVHTP_WS_MAGIC_SZ);

    sha1_init(&sha);
    sha1_update(&sha, (uint8_t *)magic_w_ws_key, magic_w_ws_key_len - 1);
    sha1_finalize(&sha, (uint8_t *)digest);

    if (base_encode(base64_rfc, digest,
                    20, (void **)&out, &out_bytes) == -1) {
        free(magic_w_ws_key);
        return -1;
    }

    out = realloc(out, out_bytes + 1);
    out[out_bytes] = '\0';

    evhtp_kvs_add_kv(hdrs_out,
                     evhtp_kv_new("Sec-WebSocket-Accept", out, 1, 1));
    free(out);
    free(magic_w_ws_key);

    if ((upgrade = evhtp_kv_find(hdrs_in, "Upgrade"))) {
        evhtp_kvs_add_kv(hdrs_out,
                         evhtp_kv_new("Upgrade", upgrade, 1, 1));
    }

    evhtp_kvs_add_kv(hdrs_out,
                     evhtp_kv_new("Connection", "Upgrade", 1, 1));
    return 0;
} /* evhtp_ws_gen_handshake */

evhtp_ws_data *
evhtp_ws_data_new(const char * data, size_t len, uint8_t opcode) {
    evhtp_ws_data * ws_data;
    uint8_t         extra_bytes;
    uint8_t         frame_len;
    size_t          ws_datalen;

    if (len <= 125) {
        frame_len = 0;
    } else if (len > 125 && len <= 65535) {
        frame_len = 126;
    } else {
        frame_len = 127;
    }

    extra_bytes         = _fext_len[frame_len];
    ws_datalen          = sizeof(evhtp_ws_data) + len + extra_bytes;

    ws_data             = calloc(ws_datalen, 1);
    ws_data->hdr.len    = frame_len ? frame_len : len;
    ws_data->hdr.fin    = 1;
    ws_data->hdr.mask   = 0;
    ws_data->hdr.opcode = opcode;

    if (frame_len) {
        memcpy(ws_data->payload, &len, extra_bytes);
    }

    memcpy((char *)(ws_data->payload + extra_bytes), data, len);

    return ws_data;
}

void
evhtp_ws_data_free(evhtp_ws_data * ws_data) {
    return free(ws_data);
}

unsigned char *
evhtp_ws_data_pack(evhtp_ws_data * ws_data, size_t * out_len) {
    unsigned char * payload_start;
    unsigned char * payload_end;
    unsigned char * res;
    uint8_t         ext_len;

    if (!ws_data) {
        return NULL;
    }

    payload_start = (unsigned char *)(ws_data->payload);

    switch (ws_data->hdr.len) {
        case 126:
            payload_end  = (unsigned char *)(payload_start + *(uint16_t *)ws_data->payload);
            payload_end += 2;
            break;
        case 127:
            payload_end  = (unsigned char *)(payload_start + *(uint64_t *)ws_data->payload);
            payload_end += 8;
            break;
        default:
            payload_end  = (unsigned char *)(payload_start + ws_data->hdr.len);
            break;
    }


    if (!(res = calloc(sizeof(evhtp_ws_frame_hdr) + (payload_end - payload_start), 1))) {
        return NULL;
    }

    *(uint16_t *)res = *(uint16_t *)&ws_data->hdr;

    memcpy((char *)(res + sizeof(uint16_t)), payload_start,
           (payload_end - payload_start));

    *out_len         = sizeof(evhtp_ws_frame_hdr) + (payload_end - payload_start);

    return res;
} /* evhtp_ws_data_pack */

evhtp_ws_parser *
evhtp_ws_parser_new(void) {
    evhtp_ws_parser *p = calloc(sizeof(evhtp_ws_parser), 1);
    if(!p)
    {
        fprintf(stderr, "calloc err, evhtp_ws line %d\n", __LINE__);
        exit(1);
    }
    return p;
}

void
evhtp_ws_parser_set_userdata(evhtp_ws_parser * p, void * usrdata) {
    assert(p != NULL);

    p->usrdata = usrdata;
}

void *
evhtp_ws_parser_get_userdata(evhtp_ws_parser * p) {
    assert(p != NULL);

    return p->usrdata;
}

void evhtp_ws_disconnect(evhtp_request_t  * req)
{
    req->disconnect=1;
}

void evhtp_ws_do_disconnect(evhtp_request_t  * req)
{
    evhtp_connection_t * c;
    struct evbuffer *b;

    if (!req)
        return;

    c = evhtp_request_get_connection(req);

    if(!c)
        return;

    /* still run the callback for disconnect */
    if (c->hooks && c->hooks->on_event) {
        (c->hooks->on_event)(c, BEV_EVENT_EOF, c->hooks->on_event_arg);
    }

    if(c->bev)
    {
         b = bufferevent_get_input(c->bev);
         evbuffer_drain(b, evbuffer_get_length(b));
    }

    if (req->ws_parser)
    {
        if(req->ws_parser->pingev)
        {
            event_del(req->ws_parser->pingev);
            event_free(req->ws_parser->pingev);
        }
        free(req->ws_parser);
    }
    evhtp_safe_free(c, evhtp_connection_free);
}
