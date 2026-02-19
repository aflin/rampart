#include "txcoreconfig.h"
#ifdef LOCK_SERVER

#include <jansson.h>
#include "texint.h"
#include "lockrequest.h"

int TXInLockBlock = 0;

DBLOCK *
opendblock(DDIC *ddic)
{
  DBLOCK *dblock = NULL;
  TXLockRequest *request = NULL, *response = NULL;
  json_t *t = NULL, *t2 = NULL;
  json_error_t e;
  TXEZsockbuf *ezsb = NULL;
  int startedlockserver = 0;
  TXPMBUF *pmbuf = TXPMBUF_SUPPRESS;

  if(!ddic) {
    return NULL;
  }
  /* TODO: Open socket to lock server */
  dblock = (DBLOCK *)TXcalloc(ddic->pmbuf, __FUNCTION__, 1, sizeof(DBLOCK));
  if(dblock) {
    /* For debugging on port 40712

    mkfifo 2way
    ncat -o logfile -l 40712 0<2way | ncat localhost 40713 1>2way

    */
opensock:
    ezsb = TXEZsockbuf_client("localhost", 40713, pmbuf);
    if(!startedlockserver && !ezsb) {
      TXrunlockdaemon(ddic);
      TXsleepmsec(100,1); /* Give lock server time to start */
      startedlockserver = 1;
      pmbuf = TXPMBUFPN;
      goto opensock;
    }
    if(!ezsb) {
      dblock = closedblock(ddic->pmbuf, dblock, 0, TXbool_False);
      goto done;
    }
    dblock->lockServerSocket = ezsb;
    if(TXApp) {
      dblock->dumponbad = TXApp->LogBadSYSLOCKS;
    } else {
      dblock->dumponbad = 0;
    }
    dblock->ddic = ddic;
    ddic->dblock = dblock;
    t = json_object();
    json_object_set_new(t, "database", json_string(ddic->pname));
    t2 = json_object();
    json_object_set_new(t2, "connect", t);
    request = TXlockRequest_CreateJson(t2);
    response = TXlockRequest(ezsb, request);
    if(!response) {
      dblock = closedblock(ddic->pmbuf, dblock, 0, TXbool_False);
      goto done;
    }
    t2 = TXlockRequest_GetJson(response);
    t = json_object_get(t2, "success");
    if(json_is_true(t)) {

    } else {
      dblock = closedblock(ddic->pmbuf, dblock, 0, TXbool_False);
      goto done;
    }
  }
done:
  if(t2) json_decref(t2);
  if(request) TXlockRequest_Close(request);
  if(response) TXlockRequest_Close(response);
  return dblock;
}

DBLOCK *
closedblock(TXPMBUF *pmbuf, DBLOCK *dbl, int sid, TXbool readOnly)
{
  if(!dbl) return dbl; /* Allow NULL struct to close */
  /* TODO: If socket open, close */
  if(dbl->lockServerSocket) {
    dbl->lockServerSocket = TXEZsockbuf_close(dbl->lockServerSocket);
  }
  return TXfree(dbl);
}

ft_counter *
getcounter(DDIC *ddic)
{
  ft_counter *rc;

	if ((rc = (ft_counter *) TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(ft_counter))) == NULL)
		return(NULL);
	else if (rgetcounter(ddic, rc, 1) <= -2)	/* complete failure */
		rc = TXfree(rc);
	return rc;
}

int
rgetcounter(DDIC *ddic, ft_counter *rc, int lock)
{
  TXLockRequest *request = NULL, *response = NULL;
  json_t *j_resp, *t;
  const char *counter_rc;
  int ret = -2;
  DBLOCK *dblock = ddic->dblock;
  static ft_counter	lcount = { 0L, 0L }, *l = &lcount;

  rc->date = time(NULL);
  if(lock && dblock) {
    request = TXlockRequest_CreateStaticString("{\"counter\":null}\n", -1);
    response = TXlockRequest(ddic->dblock->lockServerSocket, request);
    if(response) {
      j_resp = TXlockRequest_GetJson(response);
      t = json_object_get(j_resp, "counter");
      if(t) {
        counter_rc = json_string_value(t);
        TXparseHexCounter(rc, counter_rc, NULL);
        ret = 0;
        lcount = *rc;
      }
      json_decref(j_resp);
    }
  }
  if(ret < 0)
  {
    ret = lock ? -1 : 0;
    if (rc->date <= l->date)
    {
      l->seq++;
      rc->seq = l->seq;
      rc->date = l->date;
    }
    else
    {
      l->seq = 0;
      l->date = rc->date;
      rc->seq = 0;
    }
  }
done:
  if(request) TXlockRequest_Close(request);
  if(response) TXlockRequest_Close(response);
  return ret;
}

#define SET_VERIFY(a) if(want_v) *want_v = (a)

static char *
lock_type_to_char(int type, int *want_v, char *lockCmd)
{
  SET_VERIFY(0);
  if(type & INDEX_VERIFY) {
    SET_VERIFY(INDEX_VERIFY);
    type -= INDEX_VERIFY;
  }
  if(type & V_LCK) {
    SET_VERIFY(V_LCK);
    type -= V_LCK;
  }
  if(lockCmd) *lockCmd = 'L';
  switch(type) {
    case 0: if(lockCmd) *lockCmd = 'Q'; return("NL");
    case R_LCK: return("PR");
    case W_LCK: return("PW");
    case INDEX_READ: return("IR");
    case INDEX_WRITE: return("IW");
  }
  printf("Unknown type %d\n", type);
  return NULL;
}

int
dblock(DDIC *ddic, ulong sid, long *tblid, int type, char *tname, ft_counter *counterp)
/* Returns -2 if `type' is/contains V_LCK/INDEX_VERIFY and `*tblid'/`tname'
 * was modified since `*counterp'; -1 on error; 0 if no mods (or INDEX_VERIFY/
 * V_LCK not set).  Lock only *not* granted if return is -1, or if `type'
 * is V_LCK/INDEX_VERIFY alone (no ...READ/...WRITE/etc.).
 * NOTE: might not issue putmsg() if LOCK_TIMEOUT error set, or if just
 * verifying.
 */
{
  int rc = -1, dov;
  char *type_c, lockCmd;
  const char *lw;
  ft_counter last_write;
  TXLockRequest *request, *response;
  json_t *request_j, *response_j, *request_details, *t;
  char *response_s, *counter_s = NULL, *counter_e, *request_s;
  char tmpbuf[1024];
  char verify;
  size_t reqlen;

  if(!tname) {
    return 0;
  }
  if(TXInLockBlock) {
    return 0;
  }
  if(ddic->nolocking)
  {
    if(0 != (type & (V_LCK | INDEX_VERIFY)))
      return TXverifysingle;
    return 0;
  }
  type_c = lock_type_to_char(type, &dov, &lockCmd);
  if(!type_c) return -1;

  switch(dov) {
    case V_LCK: verify = 'T'; break;
    case INDEX_VERIFY: verify = 'I'; break;
    default: verify = '_'; break;
  }
  reqlen = snprintf(tmpbuf, sizeof(tmpbuf), "%c:%s:%c:%s\n", lockCmd, type_c, verify, tname);
  if(reqlen < sizeof(tmpbuf)) {
    request = TXlockRequest_CreateStaticString(tmpbuf, reqlen);
  } else {
    request_s = malloc(reqlen) + 1;
    sprintf(request_s, "%c:%s:%c:%s\n", lockCmd, type_c, verify, tname);
    request = TXlockRequest_CreateString(request_s, reqlen);
  }
  response = TXlockRequest(ddic->dblock->lockServerSocket, request);
  response_s = TXlockRequest_GetString(response, NULL);
  if(response_s && (strlen(response_s) > 4)) {
    if(*response_s == 'Y')
    {
      rc = 0;
      if(dov == V_LCK || dov == INDEX_VERIFY) {
        counter_s = response_s + 5;
#ifdef NEVER
        switch(dov) {
          case V_LCK:
            break;
          case INDEX_VERIFY:
            counter_s = strchr(counter_s, ':') + 1;
            break;
        }
#endif
        if(!*counter_s) goto done;
        counter_e = counter_s;
        while(*counter_e && *counter_e != ':') counter_e++;
        TXparseHexCounter(&last_write, counter_s, counter_e);
        if(counterp && (counterp->date != last_write.date || counterp->seq != last_write.seq)) {
          rc = -2;
        }
      }
    }
  }
done:
  if(TXgetlockverbose()) {
    printf("%s: %d %s - %d\n", __FUNCTION__, type, type_c, rc);
  }
  TXlockRequest_Close(request);
  TXlockRequest_Close(response);
  return rc;
}

int
dbunlock(DDIC *ddic, ulong sid, long *tblid, int type, char *tname)
{
  int rc = -1;
  char *type_c, *request_s, *response_s;
  TXLockRequest *request, *response;
  char tmpbuf[1024];
  size_t reqlen;

  if(!tname) {
    return 0;
  }
  if(TXInLockBlock) {
    return 0;
  }
  if(ddic->nolocking) {
    return 0;
  }
  type_c = lock_type_to_char(type, NULL, NULL);
  if(!type_c) return -1;

  reqlen = snprintf(tmpbuf, sizeof(tmpbuf), "U:%s:%s\n", type_c, tname);
  if(reqlen < sizeof(tmpbuf)) {
    request = TXlockRequest_CreateStaticString(tmpbuf, reqlen);
  } else {
    request_s = malloc(reqlen) + 1;
    sprintf(request_s, "U:%s:%s\n", type_c, tname);
    request = TXlockRequest_CreateString(request_s, reqlen);
  }
  response = TXlockRequest(ddic->dblock->lockServerSocket, request);
  response_s = TXlockRequest_GetString(response, NULL);
  if(response_s && *response_s == 'Y')
    rc = 0;
  TXlockRequest_Close(request);
  TXlockRequest_Close(response);
  return rc;
}

int
addltable(TXPMBUF	*pmbuf, DBLOCK	*sem, char	*table)
{
  return 0;
}

int
delltable(TXPMBUF	*pmbuf, DBLOCK	*sem, char	*table, int tblid)
{
  return 0;
}

int
TXsetsleepparam(unsigned int param, int value)
{
  return 0;
}

int
TXsetfairlock(int n)
{
  /* TODO */
  return -1;
}

#ifdef __MINGW32__
PID_T
#else
int
#endif
TXddicGetDbMonitorPid(DDIC *ddic)
{
  /* TODO: */
  return 0;
}

#endif /* LOCK_SERVER */
