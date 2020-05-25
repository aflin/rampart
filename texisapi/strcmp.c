#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

/* SQL stringcompare() implementation. */

int
TXfunc_stringcompare(FLD *aFld, FLD *bFld, FLD *modeFld)
/* SQL function stringcompare(a, b[, mode])
 * `modeFld' is optional.  All 3 args must be varchar.
 * Returns 0 on success, else FOP_... error.
 */
{
  static CONST char     fn[] = "TXfunc_stringcompare";
  int                   ret;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  TXCFF                 mode;
  CONST char            *aStr, *bStr, *modeStr;
  size_t                aLen, bLen, modeLen;
  ft_int                cmp, *resData = NULL;

  /* Validate args: - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (aFld == FLDPN ||
      (aFld->type & DDTYPEBITS) != FTN_CHAR ||
      bFld == FLDPN ||
      (bFld->type & DDTYPEBITS) != FTN_CHAR ||
      (modeFld != FLDPN && (modeFld->type & DDTYPEBITS) != FTN_CHAR))
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                "Wrong argument type(s) or missing args: expected [var]char");
      ret = FOP_EINVAL;
      goto done;
    }

  /* Get the string args: - - - - - - - - - - - - - - - - - - - - - - - - - */
  aStr = (char *)getfld(aFld, &aLen);
  bStr = (char *)getfld(bFld, &bLen);
  if (aStr == CHARPN || bStr == CHARPN) goto nullStrs;

  /* Get the mode: - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (globalcp == APICPPN) globalcp = TXopenapicp();
  if (modeFld != FLDPN)
    {
      modeStr = (char *)getfld(modeFld, &modeLen);
      if (modeStr == CHARPN)
        {
        nullStrs:
          txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "NULL string argument(s)");
          ret = FOP_EINVAL;
          goto done;
        }
      if (!TXstrToTxcff(modeStr, modeStr + modeLen, globalcp->textsearchmode,
                        globalcp->stringcomparemode,
                        globalcp->stringcomparemode, 1, (TXCFF)0, &mode))
        txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                       "Invalid stringcomparemode `%s'", modeStr);
    }
  else                                          /* no arg: use APICP mode */
    mode = globalcp->stringcomparemode;

  /* Do the compare: - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  cmp = TXunicodeStrFoldCmp(&aStr, aLen, &bStr, bLen, mode);

  /* Set return value: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  releasefld(aFld);                             /* before changing type */
  aFld->type = FTN_INT;
  aFld->elsz = sizeof(ft_int);
  if (!(resData = (ft_int *)TXcalloc(pmbuf, fn, 2, sizeof(ft_int))))
    {
      ret = FOP_ENOMEM;
      goto done;
    }
  *resData = cmp;
  setfldandsize(aFld, resData, sizeof(ft_int) + 1, FLD_FORCE_NORMAL);
  resData = NULL;                               /* `aFld' owns it now */
  ret = 0;                                      /* success */

done:
  resData = TXfree(resData);
  return(ret);
}
