#include "txcoreconfig.h"
#include "texint.h"
#include "ezsockbuf.h"

TXEZsockbuf *
TXEZsockbuf_client(char *hostname, int port, TXPMBUF *pmbuf)
{
  TXEZsockbuf *res = NULL;
  TXsockaddr sa;

  res = TXcalloc(NULL, __FUNCTION__, 1, sizeof(TXEZsockbuf));

  TXhostAndPortToSockaddrs(NULL, TXbool_True, TXtraceDns_None, __FUNCTION__, TXaddrFamily_IPv4, hostname, port, TXbool_True, TXbool_False, &sa, 1);
  res->socket = TXezClientSocket(pmbuf, 0, __FUNCTION__, &sa, TXbool_False, -1.0);
  if(res->socket < 0) {
    return TXEZsockbuf_close(res);
  }
  return res;
}

int
TXEZsockbuf_putbuffer(TXEZsockbuf *ezsb, char *data, size_t len)
{
  size_t wroteLen;

  while(len) {
    wroteLen = ezswrite(ezsb->socket, data, len);
    if(wroteLen >= 0) {
      len -= wroteLen;
      data += wroteLen;
    }
  }
  return 0;
}

int TXEZsockbuf_putline(TXEZsockbuf *ezsb, char *data, size_t len)
{
  char *newline = "\n";

//  printf("Sending: %s\n", data);

  TXEZsockbuf_putbuffer(ezsb, data, len);
  TXEZsockbuf_putbuffer(ezsb, newline, strlen(newline));
  return 0;
}

#define BUFINC 0x8000

static int
growbuffer(TXEZsockbuf *ezsb)
{
  if(ezsb->bufsz == 0) {
    ezsb->buffer = TXcalloc(NULL, __FUNCTION__, BUFINC, sizeof(char));
    ezsb->freesz = ezsb->bufsz = BUFINC;
    ezsb->availsz = 0;
    ezsb->head = ezsb->tail = ezsb->buffer;
  }
  return ezsb->freesz;
}

char *
TXEZsockbuf_getline(TXEZsockbuf *ezsb)
{
  char *r = NULL;
  size_t resplen;

  if(ezsb->freesz < 256) {
    growbuffer(ezsb);
  }
  resplen = TXezSocketRead(TXPMBUFPN, HtTraceSkt, __FUNCTION__, ezsb->socket, NULL,
    ezsb->head, ezsb->freesz -1, TXbool_False, NULL, TXezSR_IgnoreNoErrs);
  r = ezsb->head;
  r[resplen] = '\0';
  if(r) {
//    printf("Received: (%d) %s\n", resplen, r);
  } else {
//    printf("Received: (%d) (null)\n", resplen);
    r = NULL;
  }
  return r;
}

TXEZsockbuf *
TXEZsockbuf_close(TXEZsockbuf *ezsb)
{
  if(ezsb->buffer) {
    ezsb->buffer = TXfree(ezsb->buffer);
    ezsb->bufsz = ezsb->freesz = ezsb->availsz = 0;
  }
  return TXfree(ezsb);
}

TXLockRequest *
TXlockRequest(TXEZsockbuf *ezsb, TXLockRequest *request)
{
  TXLockRequest *ret = NULL;
  json_t *response = NULL;
  json_error_t e;
  char *req, *resp;
  double start, end, diff;
  char reqbuf[1024];
  size_t reqlen;

  switch(request->type) {
    case TX_LOCK_REQUEST_JSON:
      reqlen = json_dumpb(request->request.json, reqbuf, sizeof(reqbuf)-3, JSON_COMPACT);
      if(reqlen < (sizeof(reqbuf)-3)) {
        reqbuf[reqlen++] = '\n';
        reqbuf[reqlen] = '\0';
        if(TXgetlockverbose()) {
          printf("LockSend: %s", reqbuf);
          start = TXgettimeofday();
        }
        TXEZsockbuf_putbuffer(ezsb, reqbuf, reqlen);
      } else {
        req = json_dumps(request->request.json,JSON_COMPACT);
        if(req) {
          reqlen = strlen(req);
          if(TXgetlockverbose()) {
            printf("LockSend: %s\n", req);
            start = TXgettimeofday();
          }
          TXEZsockbuf_putline(ezsb, req, reqlen);
          free(req);
        } else {
          return NULL;
        }
      }
      break;
    case TX_LOCK_REQUEST_STRING:
      req = request->request.string.data;
      reqlen = request->request.string.len;
      if(TXgetlockverbose()) {
        printf("LockSend: %s\n", req);
        start = TXgettimeofday();
      }
      TXEZsockbuf_putbuffer(ezsb, req, reqlen);
      break;
    default: return NULL;
  }
  resp = TXEZsockbuf_getline(ezsb);
  if(resp) {
    if(TXgetlockverbose()) {
      end = TXgettimeofday();
      diff = end - start;
      printf("LockResp: %g %s", diff, resp);
    }
    ret = TXlockRequest_CreateStaticString(resp, -1);
  }
  return ret;
}
