#include "texint.h"

/* ------------------------------ SQL utils: ------------------------------ */

void *
TXsqlGetFunctionArgData(pmbuf, fn, fld, ftnType, ftiType, dataLen)
TXPMBUF         *pmbuf;         /* (in, opt.) buffer for msgs */
CONST char      *fn;            /* (in, opt.) function for msgs */
FLD             *fld;           /* (in) field to get data from */
int             ftnType;        /* (in) required FTN type of data */
FTI             ftiType;        /* (in, opt.) required FTI type of data */
size_t          *dataLen;       /* (out, opt.) # items of data */
/* getfld() wrapper that does some sanity checks, and returns ft_internal
 * object for FTN_INTERNALs.
 */
{
  void          *data;
  FTI           fldFtiType;
  ft_internal   *ftInternalData;

  if (!fld)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "NULL argument");
      goto err;
    }
  if ((fld->type & DDTYPEBITS) != (ftnType & DDTYPEBITS))
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
               "Wrong FTN type %s for SQL function argument: expected %s%s%s",
                     TXfldtypestr(fld), ddfttypename(ftnType),
                     (ftiType != FTI_UNKNOWN ? ":" : ""),
                     (ftiType != FTI_UNKNOWN ? tx_fti_type2str(ftiType) :""));
      goto err;
    }
  data = getfld(fld, dataLen);
  if (!data)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "NULL field data in SQL function argument");
      goto err;
    }
  if (ftnType == FTN_INTERNAL && ftiType != FTI_UNKNOWN)
    {                                           /* check for `ftiType' */
      ftInternalData = (ft_internal *)data;
      fldFtiType = tx_fti_gettype(ftInternalData);
      if (fldFtiType != ftiType)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                   "Wrong FTI type %s for SQL function argument: expected %s",
                         tx_fti_type2str(fldFtiType),
                         tx_fti_type2str(ftiType));
          goto err;
        }
      data = tx_fti_getobj(ftInternalData);
      if (!data)
        {
          txpmbuf_putmsg(pmbuf, MERR, fn,
                         "NULL ft_internal object in SQL function argument");
          goto err;
        }
    }
  return(data);
err:
  if (dataLen) *dataLen = 0;
  return(NULL);
}

int
TXsqlSetFunctionReturnData(CONST char *fn, FLD *fld, void *data, FTN ftnType,
                           FTI ftiType, size_t elsz, size_t numEls, int dupIt)
/* setfld() wrapper that creates an ft_internal object wrapper for `data'
 * if `ftiType != FTI_UNKNOWN'.  Dups `data' if `dupIt' is nonzero
 * (ignored if `ftiType != FTI_UNKNOWN').
 * Returns 0 on error (and frees `data' if `!dupIt' and
 * `ftiType == FTI_UNKNOWN').
 */
{
  ft_internal   *fti = NULL;
  void          *dupData;
  size_t        byteLen;
  TXPMBUF       *pmbuf = NULL;

  /* Sanity check: */
  byteLen = numEls*elsz;
  if (elsz <= (size_t)0 ||
      TX_SIZE_T_VALUE_LESS_THAN_ZERO(numEls) ||
      byteLen < TX_MIN(elsz, numEls))           /* overflow? */
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Invalid elsz/numEls %wd/%wd",
                     (EPI_HUGEINT)elsz, (EPI_HUGEINT)numEls);
      goto err;
    }

  if (ftiType != FTI_UNKNOWN)
    {
      fti = tx_fti_open(ftiType, CHARPN, 0);
      if (!fti) goto err;
      tx_fti_setobj(fti, data);
      data = fti;
    }

  TXfreefldshadow(fld);
  fld->type = ftnType;
  fld->elsz = elsz;
  if (dupIt)
    {
      if (!(dupData = TXmalloc(pmbuf, fn, byteLen + 1))) goto err;
      memcpy(dupData, data, byteLen);
      ((char *)dupData)[byteLen] = '\0';
      data = dupData;
    }
  setfldandsize(fld, data, byteLen + 1, FLD_FORCE_NORMAL);
  data = NULL;
  return(1);                                    /* success */
err:
  if (!dupIt && ftiType == FTI_UNKNOWN) data = TXfree(data);
  return(0);                                    /* failure */
}
