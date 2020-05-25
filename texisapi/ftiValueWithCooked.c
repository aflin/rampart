#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

/* A TXftiValueWithCooked object is a wrapper for a simple FTN value
 * (e.g. strlst etc.; generally non-ft_internal) that allows a cooked
 * value (e.g. parsed value) to be added later, for re-use during
 * multiple calls.
 */

struct TXftiValueWithCooked_tag
{
  FTN                                   type;
  void                                  *value; /* `type' data */
  size_t                                valueNumEls, valueSize;
  TXbool                                ownValue;       /* is it ours? */

  void                                  *cooked;
  TXftiValueWithCooked_CloseCookedFunc  *closeCooked;
};

/* ------------------------------------------------------------------------ */

static TXbool
TXftiValueWithCooked_CloseCooked(TXPMBUF *pmbuf,
                                 TXftiValueWithCooked *valueWithCooked)
{
  TXbool        ret = TXbool_True;

  if (valueWithCooked->cooked)
    {
      if (valueWithCooked->closeCooked)
        valueWithCooked->closeCooked(TXPMBUFPN, valueWithCooked->cooked);
      else
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
"Internal error: Cooked set with no closeCooked() function: Will be orphaned");
          ret = TXbool_False;
        }
      valueWithCooked->cooked = NULL;
    }
  return(ret);
}

/* ------------------------------------------------------------------------ */

TXbool
TXftiValueWithCooked_GetCookedAndCloseFunc(TXftiValueWithCooked
                                           *valueWithCooked,
           void **cooked, TXftiValueWithCooked_CloseCookedFunc **closeCooked)
{
  if (cooked) *cooked = valueWithCooked->cooked;
  if (closeCooked) *closeCooked = valueWithCooked->closeCooked;
  return(TXbool_True);
}

TXbool
TXftiValueWithCooked_SetCookedAndCloseFunc(TXPMBUF *pmbuf,
           TXftiValueWithCooked *valueWithCooked,
           void *cooked, TXftiValueWithCooked_CloseCookedFunc *closeCooked)
{
  TXbool        ret;

  if (cooked && !closeCooked)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Internal error: Cannot set cooked without close func");
      goto err;
    }
  TXftiValueWithCooked_CloseCooked(pmbuf, valueWithCooked);
  valueWithCooked->cooked = cooked;
  valueWithCooked->closeCooked = closeCooked;
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

void *
TXftiValueWithCooked_GetValue(TXftiValueWithCooked *valueWithCooked,
                              FTN *type, size_t *n, size_t *sz)
{
  if (type) *type = valueWithCooked->type;
  if (n) *n = valueWithCooked->valueNumEls;
  if (sz) *sz = valueWithCooked->valueSize;
  return(valueWithCooked->value);
}

TXbool
TXftiValueWithCooked_SetValue(TXPMBUF *pmbuf,
                              TXftiValueWithCooked *valueWithCooked,
                        void *value, FTN type, size_t n, size_t sz, TXdup dup)
/* Sets `valueWithCooked' value to `value', duping it per `dup'.
 * Returns true on success.
 */
{
  TXbool        ret;

  /* Release old value: */
  TXftiValueWithCooked_CloseCooked(pmbuf, valueWithCooked);
  if (valueWithCooked->ownValue)
    TXftnFreeData(valueWithCooked->value, valueWithCooked->valueNumEls,
                  valueWithCooked->type, 1);
  valueWithCooked->value = NULL;

  /* Set new value: */
  valueWithCooked->type = type;
  if (!value) dup = TXdup_IsPermanent;
  switch (dup)
    {
    case TXdup_DupIt:
      valueWithCooked->value = TXftnDupData(value, n, type, sz, NULL);
      if (!valueWithCooked->value) goto err;
      valueWithCooked->ownValue = TXbool_True;
      break;
    case TXdup_TakeAndFree:
      valueWithCooked->value = value;
      valueWithCooked->ownValue = TXbool_True;
      break;
    case TXdup_IsPermanent:
      valueWithCooked->value = value;
      valueWithCooked->ownValue = TXbool_False;
      break;
    default:
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__, "Invalid dup value %d",
                     (int)dup);
      goto err;
    }
  valueWithCooked->valueNumEls = n;
  valueWithCooked->valueSize = sz;
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

/* ------------ FTN_INTERNAL methods; called by ftinternal.c  ------------- */

void *
tx_fti_valueWithCooked_open(const char *usr, size_t sz)
{
  TXftiValueWithCooked  *valueWithCooked;

  (void)usr;
  (void)sz;
  valueWithCooked = TX_NEW(TXPMBUFPN, TXftiValueWithCooked);
  return(valueWithCooked);
}

void *
tx_fti_valueWithCooked_close(void *obj)
{
  TXftiValueWithCooked  *valueWithCooked = (TXftiValueWithCooked *)obj;

  if (!valueWithCooked) goto finally;

  if (valueWithCooked->ownValue)
    TXftnFreeData(valueWithCooked->value, valueWithCooked->valueNumEls,
                  valueWithCooked->type, 1);
  valueWithCooked->value = NULL;
  TXftiValueWithCooked_CloseCooked(TXPMBUFPN, valueWithCooked);
  valueWithCooked = TXfree(valueWithCooked);

finally:
  return(NULL);
}

void *
tx_fti_valueWithCooked_dup(void *obj)
{
  TXftiValueWithCooked  *src = (TXftiValueWithCooked *)obj;
  TXftiValueWithCooked  *newObj = NULL;

  newObj = TX_NEW(TXPMBUFPN, TXftiValueWithCooked);
  if (!newObj) goto err;
  newObj->type = src->type;
  if (src->value &&
      !(newObj->value = TXftnDupData(src->value, src->valueNumEls, src->type,
                                     src->valueSize, NULL)))
    goto err;
  newObj->valueNumEls = src->valueNumEls;
  newObj->valueSize = src->valueSize;
  newObj->ownValue = TXbool_True;
  goto finally;

err:
  newObj = tx_fti_valueWithCooked_close(newObj);
finally:
  return(newObj);
}

const char *
tx_fti_valueWithCooked_tostr(void *obj)
{
  (void)obj;
  /* Could return fldtostr(obj->value), but may want to know this is
   * internal.  fldtostr() of parent field will drill into valueWithCooked
   * anyway:
   */
  return("[valueWithCooked]");
}
