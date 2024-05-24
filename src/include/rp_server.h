/* Copyright (C) 2024 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#if !defined(RP_SERV_INCLUDED)
#define RP_SERV_INCLUDED

#include "rampart.h"


typedef struct {
    duk_context *ctx;
    void *dhs;
} rpserv;

void *rp_get_dhs(duk_context *ctx);

/* 
This header allows modules for the rampart-server to be written in c without
the need for a deep dive into the duktape api.

To write a rampart module that runs directly from the server:

Start with this skeleton c:

    #include "rp_server.h"

    static duk_ret_t my_cfunc(duk_context *ctx)
    {
        // initialize the server context struct
        INIT_RPSERV(serv);

        //example: get a malloced string - the json of the req object
        char *reply = rp_server_get_req_json(serv);

        // your code here


        // function must end by calling rp_server_put_reply*
        // and must return 1;

        // example: return "reply as text/plain and free it. return 1
        return rp_server_put_reply_and_free(serv, 200, "txt", reply, strlen(reply));

        //OR:
        rp_server_put_reply_string(src, 200, "txt", reply);
        free(reply);
        return 1;
    }

    // Initialize module
    duk_ret_t duk_open_module(duk_context *ctx)
    {
        duk_push_c_function(ctx, my_cfunc, 1);

        return 1;
    }

Compile it as such
    Linux:
        cc -I/usr/local/rampart/include -fPIC -shared -Wl,-soname,mymod.so -o mymod.so mymod.c
    Macos:
        cc -I/usr/local/rampart/include -dynamiclib -undefined dynamic_lookup -install_name mymod.so -o mymod.so mymod.c

Put the file (e.g. "mymod.so") into the web_server/apps/ directory (or wherever your webscripts live).

*If you use other duktape API functions along with any of the rp_server_get* functions:
    
    1) The request object will be on the value stack at index 0.  
    
    2) If you move or delete the request object at index 0, the rp_server_get* functions
       will no longer work.

See (https://duktape.org/api.html for more info on the duktape API)


See below for description of functions.

*/


// initialize the server context struct
// put a stash array in req
#define INIT_RPSERV(_serv, _duk_ctx) \
rpserv _rpserv_struct;\
rpserv * _serv = &_rpserv_struct;\
_serv->ctx=_duk_ctx;\
_serv->dhs=rp_get_dhs(_duk_ctx);\
duk_push_array(_duk_ctx);\
duk_put_prop_string(_duk_ctx, 0, DUK_HIDDEN_SYMBOL("rp_serv_stash"));


typedef struct {
    void         *value;
    size_t        length;
    const char   *file_name;
    const char   *name;
    const char   *content_type;
    const char   *content_disposition;
} multipart_postvar;

//GET FUNCTIONS (see: https://rampart.dev/docs/rampart-server.html#the-request-object )
/*
NOTE: except for multipart form data, all values returned will be strings.  If value
      is repeated in posted form data, then it will be returned as a JSON string. E.g:

      http://localhost:8088/apps/my_mod/?x=val1&x=val2
          x = "[\"val1\", \"val2\"]"

      http://localhost:8088/apps/my_mod/?x[key1]=val1&x[key2]=val2
          x = "{\"key1\":\"val1\", \"key2\":\"val2\"}"

      http://localhost:8088/apps/my_mod/?x[key1]=val1&x=val2
          x = "{\"0\":\"val2\", \"key1\":\"val1\"}
*/

// get a parameter by name
const char * rp_server_get_param(rpserv *serv, char *name);

// get a header by name
const char * rp_server_get_header(rpserv *serv, char *name);

// get a query string parameter by name
const char * rp_server_get_query(rpserv *serv, char *name);

// get a path component ( name is [file|path|base|scheme|host|url] )
const char * rp_server_get_path(rpserv *serv, char *name);

// get a parsed cookie
const char * rp_server_get_cookie(rpserv *serv, char *name);

// get posted body content as a void buffer.
void * rp_server_get_body(rpserv *serv, size_t *sz);

// get a malloced string of the current request object (just like in
// https://rampart.dev/docs/rampart-server.html#the-request-object ) 
char * rp_server_get_req_json(rpserv *serv);

// get an array of strings, the names (keys) of all [parameters|headers|paths|cookies].  
// If "values" is not null, also set values to the corresponding parameter values.
/* example:
    int i=0;
    const char **vals, *val, *key;
    const char **keys = rp_server_get_params(serv, &vals);

    while(keys) //keys will not be null (unless program errantly moves/dels req at idx=0 on duktape stack)
    {
        key=paramkeys[i];
        if(!key)
            break;
        val=vals[i];
        i++;
        //do something here with key & val
    }


*/
const char ** rp_server_get_params(rpserv *serv, const char ***values);

const char ** rp_server_get_headers(rpserv *serv, const char ***values);

const char ** rp_server_get_paths(rpserv *serv, const char ***values);

const char ** rp_server_get_cookies(rpserv *serv, const char ***values);

// get the number of "parts" in a multipart/form-data post
int rp_server_get_multipart_length(rpserv *serv);

// retrieve the multipart variable and metadata at position "index"
multipart_postvar rp_server_get_multipart_postitem(rpserv *serv, int index);


//PUT FUNCTIONS:

// add the contents of *data to buffer to be returned to client
void rp_server_put(rpserv *serv, void *buf, size_t bufsz);

// add the contents of the null terminated *data to buffer to be returned to client
void rp_server_put_string(rpserv *serv, char *s);

// same as rp_server_put, but takes a malloced string and frees it.
// Using this function with malloced data saves a copy and a free.
void rp_server_put_and_free(rpserv *serv, void *buf, size_t bufsz);

// same as rp_server_put_and_free, but takes a null terminated string.
void rp_server_put_string_and_free(rpserv *serv, char *s);



// END FUNCTIONS:
//   1) One and only one of these should be called at or near the end of the main function
//   2) Each function returns (duk_ret_t)1;

// return HTTP Code "code", using a mime type that matches *ext, e.g. "html", "txt", "json", etc.
// (for ext->mime_type map, see https://rampart.dev/docs/rampart-server.html#key-to-mime-mappings)
// If all the content to be sent to client has already been added via two above functions,
// set buf to NULL and bufsz to 0.
// Otherwise to append more content, set buf and bufsz as appropriate.
duk_ret_t rp_server_put_reply(rpserv *serv, int code, char *ext, void *buf, size_t bufsz);

// Same as above, but *s is either a null terminated string or NULL.
duk_ret_t rp_server_put_reply_string(rpserv *serv, int code, char *ext, char *s);

// Same as rp_server_put_reply, but takes a malloced string and frees it.
// Using this function with malloced data saves a copy and a free.
duk_ret_t rp_server_put_reply_and_free(rpserv *serv, int code, char *ext, void *buf, size_t bufsz);

// Same as rp_server_put_reply_and_free, but takes a null terminated string.
duk_ret_t rp_server_put_reply_string_and_free(rpserv *serv, int code, char *ext, char *s);

#endif
