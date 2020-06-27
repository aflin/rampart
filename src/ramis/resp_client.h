//
//  resp_client.h
//  ramis_client
//
//  Created by Dr Cube on 5/20/20.
//  Copyright © 2020 P. B. Richards. All rights reserved.
//

#if !defined(RESP_CLIENT_H)
#define RESP_CLIENT_H

#include <stdarg.h>
#include "ramis.h"
#include "resp_protocol.h"

#define RESPCLIENTBUFSZ 8192 // Transmit and recieve buffer size
#define RESPCLIENTTIMEOUT 3  // Number of seconds to wait for a response
#define RESPMAXDIGITS 50     // Maximum number of ascii digits in a rendered number

// these are for respCommandArgTypes(char *fmt,int *nArgs)

#define RESPCLIENT struct RespClientStruct
RESPCLIENT
{
  RESPROTO *rppFrom;
  byte *fromBuf;      // where we put junk from the server
  byte *fromReadp;    // where the next read from server will go
  size_t fromBufSize; // how big is the buffer overall now
  byte *toBuf;        // where we stage stuff destined for the server
  size_t toBufSz;     // the toBuf's current size
  int socket;         // the raw socket
  char *hostname;     // these are kept from the initial open so we can reconnect
  int port;
  int waitForever; // disables RESPCLIENTTIMEOUT for SUBSCRIBE commands
};

// https://stackoverflow.com/questions/5891221/variadic-macros-with-zero-arguments explains the ## below
//#define sendRespCommand(rcp,fmtString,...) {respPrintf((rcp)->rppTo,(rcp)->fhToServer,fmtString, ## __VA_ARGS__);}

// gets a reply from the RESP server and parses it into items list within the RESPROTO struct
RESPROTO *getRespReply(RESPCLIENT *rcp);

// closes and reopens the connection to the server and resets the buffers
int reconnectRespServer(RESPCLIENT *rcp);

// connect to the RESP Server
RESPCLIENT *connectRespServer(char *hostname, int port);

// disconnect and free resources
RESPCLIENT *closeRespClient(RESPCLIENT *rcp);

// a formatted way to send data to the server
RESPROTO *sendRespCommand(RESPCLIENT *rcp, char *fmt, ...);

// Counts the number of arguments to expect for sendRespCommand()
// places the count in *nArgs
// returns an array containing the type of each arg
int *respCommandArgTypes(char *fmt, int *nArgs);

// Sees if anything went wrong. If everything's ok returns NULL , otherwise an error message.
char *respClienError(RESPCLIENT *rcp);

#define respClientWaitForever(rcp, true_false) rcp->waitForever = true_false

#if !defined(RP_USING_DUKTAPE)

#define RP_VA_ARG
#define VA_ARG(a, b) va_arg(a, b)
#define VA_END(a) va_end(a)

#else

#define RP_VA_RET union rp_va_u
RP_VA_RET
{
  char *c;
  size_t s;
  int i;
  long l;
  long long L;
  double d;
  unsigned u;
  unsigned long U;
  unsigned long long I;
};

RP_VA_RET duk_rp_getarg(duk_context *ctx, const char *type);

#define RP_VA_ARG duk_context *ctx = (duk_context *)va_arg(arg, void *);

#define VA_ARG(a, b) ({                 \
  RP_VA_RET u = duk_rp_getarg(ctx, #b); \
  *((b *)(&u));                         \
})

#define VA_END(a)                                                   \
  do                                                                \
  {                                                                 \
    duk_pull(ctx, 1);                                               \
    if (!duk_is_undefined(ctx, -1))                                 \
    {                                                               \
      duk_push_string(ctx, "too many arguments for exec(fmt,...)"); \
      duk_throw(ctx);                                               \
    }                                                               \
    va_end(a);                                                      \
  } while (0)

#endif /* RP_USING_DUKTAPE */

#endif /* RESP_CLIENT_H */
