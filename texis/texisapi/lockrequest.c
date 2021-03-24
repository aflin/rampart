#include "txcoreconfig.h"
#ifdef LOCK_SERVER

#include <jansson.h>
#include "texint.h"
#include "lockrequest.h"

static TXbool lockverbose = TXbool_False;

#define TXF(a) a ? free(a), (void *)NULL : (void *)NULL

TXLockRequest *
TXlockRequest_CreateJson(json_t *t)
{
  TXLockRequest *ret = NULL;
  if(t) {
    ret = calloc(1, sizeof(TXLockRequest));
    if(ret) {
      ret->type = TX_LOCK_REQUEST_JSON;
      ret->request.json = t;
    }
  }
  return ret;
}

TXLockRequest *
TXlockRequest_CreateStaticString(char *t, size_t len)
{
  TXLockRequest *ret = NULL;
  if(t) {
    ret = (TXLockRequest *)calloc(1, sizeof(TXLockRequest));
    if(ret) {
      ret->type = TX_LOCK_REQUEST_STRING;
      ret->request.string.data = t;
      if(len == -1) {
        ret->request.string.len = strlen(t);
      } else {
        ret->request.string.len = len;
      }
      ret->request.string.alloced = 0;
    }
  }
  return ret;
}

TXLockRequest *
TXlockRequest_CreateString(char *t, size_t len)
{
  TXLockRequest *ret = TXlockRequest_CreateStaticString(t, len);
  if(ret) {
    ret->request.string.alloced = 1;
  }
  return ret;
}

json_t *
TXlockRequest_GetJson(TXLockRequest *lr)
{
  json_t *ret = NULL;
  json_error_t e;

  if(lr) {
    switch(lr->type) {
      case TX_LOCK_REQUEST_JSON:
        ret = lr->request.json;
        break;
      case TX_LOCK_REQUEST_STRING:
        ret = json_loads(lr->request.string.data, 0, &e);
        if(lr->request.string.alloced) {
          lr->request.string.data = TXF(lr->request.string.data);
          lr->request.json = ret;
          lr->type = TX_LOCK_REQUEST_JSON;
        }
        break;
    }
  }
  return ret;
}

char *
TXlockRequest_GetString(TXLockRequest *lr, size_t *len)
{
  char *ret = NULL;

  if(lr) {
    switch(lr->type) {
      case TX_LOCK_REQUEST_STRING:
        ret = lr->request.string.data;
        if(len) {
          *len = lr->request.string.len == -1 ? strlen(ret) : lr->request.string.len;
        }
        break;
    }
  }
  return ret;
}

TXLockRequest *
TXlockRequest_Close(TXLockRequest *lr)
{
  if(lr) {
    switch(lr->type) {
      case TX_LOCK_REQUEST_JSON:
        json_decref(lr->request.json);
        lr->request.json = NULL;
        break;
      case TX_LOCK_REQUEST_STRING:
        if(lr->request.string.alloced) {
          lr->request.string.data = TXF(lr->request.string.data);
          lr->request.string.alloced = 0;
        }
        break;
    }
    lr = TXF(lr);
  }
  return lr;
}

TXbool
TXsetlockverbose(int n)
{
  TXbool r = lockverbose;
  lockverbose = n;
  return r;
}
int
TXgetlockverbose(void)
{
  return lockverbose;
}
#endif /* LOCK_SERVER */
