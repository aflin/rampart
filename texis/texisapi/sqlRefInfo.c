#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "texint.h"
#include "htmll.h"
#include "http.h"


/* SQL refInfo...() functions. */

#ifdef EPI_REFINFO_API
#  define TXrefInfoApiEnabled(pmbuf) 1
#else /* !EPI_REFINFO_API */
static int
TXrefInfoApiEnabled(TXPMBUF *pmbuf)
{
  static int    enabled = -1;

  if (enabled == -1)
    {
      enabled = (getenv("EPI_REFINFO_API") != NULL);
      if (!enabled) txpmbuf_putmsg(pmbuf, MERR + UGE, NULL,
                                   "refInfo API not supported yet");
    }
  return(enabled);
}
#endif /* !EPI_REFINFO_API */

/* ---------------------------- ft_internal glue: --------------------------*/

void *
tx_fti_refInfo_open(const char *usr, size_t sz)
{
  (void)usr;
  (void)sz;
  /* Do nothing: actual open handled via SQL or fetch function. */
  return(NULL);
}

void *
tx_fti_refInfo_close(void *obj)
{
  TXrefInfo     *refInfo = (TXrefInfo *)obj;
  
  if (!refInfo) return(NULL);

  refInfo = TXrefInfoClose(refInfo);
  return(NULL);
}

void *
tx_fti_refInfo_dup(void *obj)
{
  /* not used? */
  (void)obj;
  putmsg(MERR + UGE, __FUNCTION__, "Internal error: Unexpected call");
  return(NULL);
}

const char *
tx_fti_refInfo_tostr(void *obj)
/* Converts ref `obj' to a string.
 */
{
  (void)obj;
  return("[refInfo]");
}

/* -------------------------  SQL helper functions: ----------------------- */

static int
TXsqlRefInfoReturnVarcharValue(FLD *refInfoFld,
                              const char * (*getValFunc)(TXrefInfo *refInfo))
/* SQL-returns varchar value of `getValFunc()'-returned value from
 * `refInfoFld'.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  static const char     fn[] = "TXsqlRefInfoReturnVarcharValue";
  TXrefInfo             *refInfo = NULL;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int                   ret;
  size_t                numRefs;
  ft_char               *value;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, fn, refInfoFld,
                                         FTN_INTERNAL, FTI_refInfo, &numRefs);
  if (!refInfo) goto err;

  value = (ft_char *)getValFunc(refInfo);
  if (!value) value = "";                       /* wtf return SQL NULL? */

  if (!TXsqlSetFunctionReturnData(fn, refInfoFld, value, FTN_varCHAR,
                              FTI_UNKNOWN, sizeof(ft_char), strlen(value), 1))
    goto err;
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  return(ret);
}

static int
TXsqlRefInfoReturnInt64Value(FLD *refInfoFld,
                             EPI_SSIZE_T (*getValFunc)(TXrefInfo *refInfo))
/* SQL-returns int64 value of `getValFunc()'-returned value from `refInfoFld'.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  static const char     fn[] = "TXsqlRefInfoReturnInt64Value";
  TXrefInfo             *refInfo = NULL;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int                   ret;
  size_t                numRefs;
  ft_int64              value;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, fn, refInfoFld,
                                         FTN_INTERNAL, FTI_refInfo, &numRefs);
  if (!refInfo) goto err;

  value = (ft_int64)getValFunc(refInfo);

  if (!TXsqlSetFunctionReturnData(fn, refInfoFld, &value, FTN_INT64,
                                  FTI_UNKNOWN, sizeof(ft_int64), 1, 1))
    goto err;
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  return(ret);
}

/* ----------------------------- SQL functions: --------------------------- */

int
TXsqlFunc_refInfoGetTypes(FLD *refInfoFld)
/* SQL-returns strlst types from `refInfoFld'.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  static const char     fn[] = "TXsqlFunc_refInfoGetTypes";
  TXrefInfo             *refInfo = NULL;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int                   ret;
  char                  *retData = NULL;
  size_t                retNumEls;
  TXrefType             refType;
  TXrefTypeFlag         refTypeFlags;
  HTBUF                 *strlstBuf = NULL;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, fn, refInfoFld,
                                             FTN_INTERNAL, FTI_refInfo, NULL);
  if (!refInfo) goto err;

  if (!(strlstBuf = openhtbuf())) goto err;
  if (!TXstrlstBufBegin(strlstBuf)) goto err;

  refTypeFlags = TXrefInfoGetTypes(refInfo);
  for (refType = (TXrefType)0; refType < TXrefType_NUM; refType++)
    if ((refTypeFlags & TX_REF_TYPE_FLAG(refType)) &&
        !TXstrlstBufAddString(strlstBuf, TXrefTypeStr(refType, "?"),
                              (size_t)(-1)))
      goto err;
  if (!TXstrlstBufEnd(strlstBuf)) goto err;
  retNumEls = htbuf_getdata(strlstBuf, &retData, 0x3);

  if (!TXsqlSetFunctionReturnData(fn, refInfoFld, retData,
                                  (DDVARBIT | FTN_STRLST), FTI_UNKNOWN, 1,
                                  retNumEls, 0))
    goto err;
  retData = NULL;                               /* owned by FLD now */
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  strlstBuf = closehtbuf(strlstBuf);
  retData = TXfree(retData);
  return(ret);
}

int
TXsqlFunc_refInfoGetFlags(FLD *refInfoFld)
/* SQL-returns strlst flags from `refInfoFld'.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  static const char     fn[] = "TXsqlFunc_refInfoGetFlags";
  TXrefInfo             *refInfo = NULL;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int                   ret;
  char                  *retData = NULL;
  size_t                retNumEls;
  TXrefFlagIter         flagIdx;
  TXrefFlag             refFlags;
  HTBUF                 *strlstBuf = NULL;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, fn, refInfoFld,
                                             FTN_INTERNAL, FTI_refInfo, NULL);
  if (!refInfo) goto err;

  if (!(strlstBuf = openhtbuf())) goto err;
  if (!TXstrlstBufBegin(strlstBuf)) goto err;

  refFlags = TXrefInfoGetFlags(refInfo);
  for (flagIdx = (TXrefFlagIter)0; flagIdx < TXrefFlagIter_NUM; flagIdx++)
    if ((refFlags & TX_REF_FLAG(flagIdx)) &&
        !TXstrlstBufAddString(strlstBuf,
                              TXrefFlagStr(TX_REF_FLAG(flagIdx), "?"),
                              (size_t)(-1)))
      goto err;
  if (!TXstrlstBufEnd(strlstBuf)) goto err;
  retNumEls = htbuf_getdata(strlstBuf, &retData, 0x3);

  if (!TXsqlSetFunctionReturnData(fn, refInfoFld, retData,
                                  (DDVARBIT | FTN_STRLST), FTI_UNKNOWN, 1,
                                  retNumEls, 0))
    goto err;
  retData = NULL;                               /* owned by FLD now */
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  strlstBuf = closehtbuf(strlstBuf);
  retData = TXfree(retData);
  return(ret);
}

int
TXsqlFunc_refInfoGetTagName(FLD *refInfoFld)
/* SQL-returns varchar tag, or empty if unknown.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  static const char     fn[] = "TXsqlFunc_refInfoGetTagName";
  TXrefInfo             *refInfo = NULL;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int                   ret;
  size_t                numRefs;
  HTAG                  tag;
  ft_char               *value;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, fn, refInfoFld,
                                         FTN_INTERNAL, FTI_refInfo, &numRefs);
  if (!refInfo) goto err;

  tag = TXrefInfoGetTag(refInfo);
  value = (ft_char *)htint2tag(tag);
  if (!value) value = "";                       /* wtf return SQL NULL? */

  if (!TXsqlSetFunctionReturnData(fn, refInfoFld, value, FTN_varCHAR,
                              FTI_UNKNOWN, sizeof(ft_char), strlen(value), 1))
    goto err;
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  return(ret);
}

int
TXsqlFunc_refInfoGetSourceAttribute(FLD *refInfoFld)
/* SQL-returns varchar attribute that made this a ref, or empty if unknown.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  TXrefInfo             *refInfo = NULL;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int                   ret;
  size_t                numRefs;
  HATTR                  attr;
  ft_char               *value;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, __FUNCTION__,
                                                 refInfoFld,
                                         FTN_INTERNAL, FTI_refInfo, &numRefs);
  if (!refInfo) goto err;

  attr = TXrefInfoGetSourceAttribute(refInfo);
  value = (ft_char *)htint2attr(attr);
  if (!value) value = "";                       /* wtf return SQL NULL? */

  if (!TXsqlSetFunctionReturnData(__FUNCTION__, refInfoFld, value,
                 FTN_varCHAR, FTI_UNKNOWN, sizeof(ft_char), strlen(value), 1))
    goto err;
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  return(ret);
}

int
TXsqlFunc_refInfoGetAttribute(FLD *refInfoFld, FLD *attrNameFld)
/* SQL-returns varchar attribute value for given `attrNameFld' attribute name,
 * or empty if unknown.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  TXrefInfo     *refInfo = NULL;
  TXPMBUF       *pmbuf = TXPMBUFPN;
  int           ret;
  size_t        numRefs, nameLen;
  ft_char       *attrName, *attrValue;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, __FUNCTION__,
                                                 refInfoFld, FTN_INTERNAL,
                                                 FTI_refInfo, &numRefs);
  if (!refInfo) goto err;
  attrName = (ft_char *)TXsqlGetFunctionArgData(pmbuf, __FUNCTION__,
                                                attrNameFld, FTN_varCHAR,
                                                FTI_UNKNOWN, &nameLen);
  if (!attrName) goto err;

  attrValue = TXrefInfoGetAttribute(refInfo, attrName);
  if (!attrValue) attrValue = "";               /* wtf return SQL NULL? */

  if (!TXsqlSetFunctionReturnData(__FUNCTION__, refInfoFld, attrValue,
                                  FTN_varCHAR, FTI_UNKNOWN, sizeof(ft_char),
                                  strlen(attrValue), 1))
    goto err;
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  return(ret);
}

int
TXsqlFunc_refInfoGetAttributes(FLD *refInfoFld)
/* SQL-returns strlst attribute names from `refInfoFld'.
 * Returns FOP_EOK on success, else negative value (FOP_E... code) on error.
 */
{
  TXrefInfo     *refInfo = NULL;
  TXPMBUF       *pmbuf = TXPMBUFPN;
  int           ret;
  char          *retData = NULL;
  size_t        retNumEls;
  HTBUF         *strlstBuf = NULL;
  char          **attrs;

  if (!TXrefInfoApiEnabled(pmbuf)) goto err;

  refInfo = (TXrefInfo *)TXsqlGetFunctionArgData(pmbuf, __FUNCTION__,
                                                 refInfoFld, FTN_INTERNAL,
                                                 FTI_refInfo, NULL);
  if (!refInfo) goto err;

  if (!(strlstBuf = openhtbuf())) goto err;
  if (!TXstrlstBufBegin(strlstBuf)) goto err;

  for (attrs = TXrefInfoGetAttributes(refInfo);
       attrs && *attrs;
       attrs++)
    if (!TXstrlstBufAddString(strlstBuf, *attrs, (size_t)(-1)))
      goto err;
  if (!TXstrlstBufEnd(strlstBuf)) goto err;
  retNumEls = htbuf_getdata(strlstBuf, &retData, 0x3);

  if (!TXsqlSetFunctionReturnData(__FUNCTION__, refInfoFld, retData,
                                  (DDVARBIT | FTN_STRLST), FTI_UNKNOWN, 1,
                                  retNumEls, 0))
    goto err;
  retData = NULL;                               /* owned by FLD now */
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  strlstBuf = closehtbuf(strlstBuf);
  retData = TXfree(retData);
  return(ret);
}

#define REFINFO_SQL_FUNC(retType, fld)          \
int                                             \
TXsqlFunc_refInfoGet##fld(FLD *refInfoFld)      \
{                                               \
  return(TXsqlRefInfoReturn##retType##Value(refInfoFld, TXrefInfoGet##fld)); \
}

#undef I
#define I(fld)  REFINFO_SQL_FUNC(Varchar, fld)
TX_REFINFO_STRING_SYMBOLS
#undef I

#undef I
#define I(fld)  REFINFO_SQL_FUNC(Int64, fld)
TX_REFINFO_SSIZE_T_SYMBOLS
#undef I
