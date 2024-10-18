#ifndef LOCK_REQUEST_H
#define LOCK_REQUEST_H

#include <jansson.h>

typedef enum TXLockRequestType {
  TX_LOCK_REQUEST_JSON,
  TX_LOCK_REQUEST_STRING
} TXLockRequestType;

typedef struct TXLockRequest {
  TXLockRequestType  type;
  union {
    json_t *json;
    struct {
      char *data;
      size_t len;
      int alloced;
    } string;
  } request;
} TXLockRequest;

TXLockRequest *TXlockRequest_CreateJson(json_t *j);
TXLockRequest *TXlockRequest_CreateStaticString(char *s, size_t len);
TXLockRequest *TXlockRequest_CreateString(char *s, size_t len);
TXLockRequest * TXlockRequest_Close(TXLockRequest *request);
json_t *TXlockRequest_GetJson(TXLockRequest *request);
char *TXlockRequest_GetString(TXLockRequest *request, size_t *len);

#endif
