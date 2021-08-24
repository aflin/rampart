#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <evhtp.h>
#include "../ws/evhtp_ws.h"

/* free the data after evbuffer is done with it */
static void refcb(const void *data, size_t datalen, void *val)
{
    if(val)
        free((void *)val);
    else
        free((void *)data);
}


void
testcb(evhtp_request_t * req, void * a) {
    int disconnect=0;
    void *buf = evbuffer_pullup(req->buffer_in, -1);

    if(buf && !strncasecmp("bye", (char*)buf, 3))
        disconnect = 1;

    /* upon connect, the first request will have headers from the
       http connect, but no ws data                               */
    if(!evbuffer_get_length(req->buffer_in))
        return;

    /* echo: use buffer_in, prepend proper header and send it back */
    evhtp_ws_add_header(req->buffer_in, req->ws_opcode);
    evhtp_send_reply_body(req, req->buffer_in);

    if(disconnect)
        evhtp_ws_disconnect(req);
}

int
main(int argc, char ** argv) {
    evbase_t * evbase = event_base_new();
    evhtp_t  * htp    = evhtp_new(evbase, NULL);

    evhtp_set_cb(htp, "ws:/", testcb, NULL);
#ifndef EVHTP_DISABLE_EVTHR
    evhtp_use_threads_wexit(htp, NULL, NULL, 8, NULL);
#endif
    evhtp_bind_socket(htp, "0.0.0.0", 8081, 2048);

    printf("A websocket echo server\nTry, e.g. wscat -c ws://localhost:8081/\nType 'bye' to exit\n");
    
    event_base_loop(evbase, 0);

    evhtp_unbind_socket(htp);
    evhtp_safe_free(htp, evhtp_free);
    evhtp_safe_free(evbase, event_base_free);

    return 0;
}
