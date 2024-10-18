#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "http.h"

typedef enum TXsqlFuncLookup_Flag_tag
{
  TXsqlFuncLookup_Flag_IncludeLower     = (1 << 0),
  TXsqlFuncLookup_Flag_IncludeUpper     = (1 << 1),
  TXsqlFuncLookup_Flag_InfiniteLower    = (1 << 2),
  TXsqlFuncLookup_Flag_InfiniteUpper    = (1 << 3),
  TXsqlFuncLookup_Flag_ImplicitUpper    = (1 << 4),
}
  TXsqlFuncLookup_Flag;

typedef enum TXsqlFuncLookup_Part_tag
{
  TXsqlFuncLookup_Part_LowerInclusivity,
  TXsqlFuncLookup_Part_LowerBound,
  TXsqlFuncLookup_Part_RangeOperator,
  TXsqlFuncLookup_Part_UpperInclusivity,
  TXsqlFuncLookup_Part_UpperBound,
  TXsqlFuncLookup_Part_NUM                      /* must be last */
}
  TXsqlFuncLookup_Part;

/* A single cooked range: */
typedef struct TXsqlFuncLookup_Range_tag
{                                               /* `rangeItemType' --v */
  void                  *data;                  /* original range FTN data */
  size_t                dataLen;                /*   and length in items */
  void                  *lowerBoundData;        /* `boundItemFld'-type data */
  size_t                lowerBoundLen;
  void                  *upperBoundData;
  size_t                upperBoundLen;
  size_t                orgBinIdx;
  TXsqlFuncLookup_Flag  flags;
  /* for info purposes; parse offsets and lengths into `data' (-1: none): */
  size_t                partDataOff[TXsqlFuncLookup_Part_NUM];
  size_t                partDataLen[TXsqlFuncLookup_Part_NUM];
  char                  *upperBoundSrc;         /* if implicit and varchar */
}
  TXsqlFuncLookup_Range;
#define TXsqlFuncLookup_Range_INVALID   ((TXsqlFuncLookup_Range *)1)

/* Cooked data for bin ranges.  Largely derived from bin ranges field,
 * though depends on type of keys field and bin names field:
 */
typedef struct TXsqlFuncLookup_RangesInfo_tag
{
  FTN                   keysType;
  /* `ranges' does not include invalid/illegal bins; hence each range
   * has an `orgBinIdx' index field to properly index back into names:
   */
  TXsqlFuncLookup_Range **ranges;               /* `numRanges' ranges */
  size_t                numRanges, numAllocedRanges;
  FTN                   rangesType, rangeItemType;
  FTN                   namesType, nameItemType;
  size_t                numOrgBins;             /* including invalid ranges */
  /* Scratch fields for TXfldCmpSameType(): */
  FLD                   *keyItemFld;
  FLD                   *boundItemFld;
  /* Return data; `returnStrlstBuf' for strlst,
   * `returnData'/`numReturn...'  for other types.  Doesn't really
   * need saving between lookup() calls, but convenient to hang onto
   * an object:
   */
  FTN                   returnItemType;
  size_t                returnItemElSz;
  FTN                   returnType;
  HTBUF                 *returnStrlstBuf;
  byte                  *returnData;
  size_t                numReturnUsedBytes, numReturnAllocedBytes;
}
  TXsqlFuncLookup_RangesInfo;

/* Cooked data for bin names (derived from bin names field): */
typedef struct TXsqlFuncLookup_NamesInfo_tag
{
  void                  **nameDatas;            /* alloced */
  size_t                *nameLens;
  size_t                numNames;
  FTN                   namesType, nameItemType;
}
  TXsqlFuncLookup_NamesInfo;

/* differentiate trace messages from rest of fldmath verbose: */
#define TRACE_LOOKUP_OPENCLOSE  1
#define TRACE_LOOKUP_CALL       2

/* ------------------------------------------------------------------------ */

FTN
TXsqlFuncLookup_GetReturnType(FTN keysType, size_t keysLen,
                              FTN rangesType, size_t rangesLen,
                              FTN namesType, size_t namesLen)
/* Returns return type of lookup() with given args.
 * NOTE: must correspond with TXsqlFuncLookup() behavior;
 * this is used by predtype() to determine lookup() return type.
 */
{
  FLD   keysFld;

  initfld(&keysFld, keysType, keysLen);
  (void)rangesLen;
  (void)namesLen;

  if (namesType)                                /* names arg present */
    {
      /* Make return type variable: different rows could have
       * different sizes.  And after Bug 6974 we could match multiple
       * bins and thus return multiple (or zero) values if `keysType'
       * *or* `namesType' is multi-item:
       */
      if ((namesType & DDTYPEBITS) == FTN_CHAR &&       /* i.e. single-item */
          TXfldIsMultipleItemType(&keysFld, NULL, NULL))
        return(FTN_varSTRLST);
      return(namesType | DDVARBIT);
    }
  /* No names.  Will be returning ranges type, or item(s) from
   * it.  See Bug 6974 comment above too:
   */
  if ((rangesType & DDTYPEBITS) == FTN_CHAR &&          /* i.e. single-item */
      TXfldIsMultipleItemType(&keysFld, NULL, NULL))
    return(FTN_varSTRLST);
  return(rangesType | DDVARBIT);
}

/* ------------------------------------------------------------------------ */

static int
TXsqlFuncLookup_AppendResultItem(TXPMBUF *pmbuf,
                                 TXsqlFuncLookup_RangesInfo *rangesInfo,
                                 TXsqlFuncLookup_NamesInfo *namesInfo,
                                 TXsqlFuncLookup_Range *range)
/* Appends a copy of `range' item (either range or its name), if
 * present, to return fields in `rangesInfo'.
 * Returns 0 on error (e.g. out of mem).
 */
{
  int                   ret;
  void                  *returnItemData;
  size_t                returnItemLen, returnItemSz;

  /* NOTE: type may vary; determination here must agree with
   * TXsqlFuncLookup_GetReturnType() and ...InfoOpen():
   */
  if (namesInfo)                                 /* returning bin *name* */
    {
      if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(range->orgBinIdx) ||
          range->orgBinIdx >= namesInfo->numNames)
        goto ok;
      returnItemData = namesInfo->nameDatas[range->orgBinIdx];
      returnItemLen = namesInfo->nameLens[range->orgBinIdx];
    }
  else                                          /* returning bin *range* */
    {
      returnItemData = range->data;
      returnItemLen = range->dataLen;
    }
  if (!returnItemData) goto ok;                 /* nothing to append */

  /* Add `returnItemData' to `returnStrlstBuf' or `*returnData': */
  if (rangesInfo->returnStrlstBuf)
    {
      if (!TXstrlstBufAddString(rangesInfo->returnStrlstBuf,
                                (char *)returnItemData, returnItemLen))
        goto err;
    }
  else
    {
      returnItemSz = rangesInfo->returnItemElSz*returnItemLen;
      if (!TX_EXPAND_ARRAY(pmbuf, &rangesInfo->returnData,
                           rangesInfo->numReturnUsedBytes,
                           &rangesInfo->numReturnAllocedBytes,
                           returnItemSz + 1))
        goto err;
      /* WTF what if FTN_INTERNAL */
      memcpy(rangesInfo->returnData + rangesInfo->numReturnUsedBytes,
             returnItemData, returnItemSz);
      rangesInfo->numReturnUsedBytes += returnItemSz;
      /* nul-terminate in case varchar: */
      rangesInfo->returnData[rangesInfo->numReturnUsedBytes] = '\0';
    }

ok:
  ret = 1;                                      /* success */
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

/* ------------------------------------------------------------------------ */

static int
TXsqlFuncLookup_FindKeyItemAndAppendBins(TXPMBUF *pmbuf,
 TXsqlFuncLookup_RangesInfo *rangesInfo, TXsqlFuncLookup_NamesInfo *namesInfo)
/* Looks up single item `rangesInfo->keyItemFld' (populated by caller), and
 * appends zero or more items' bins to return fields in `rangesInfo'.
 * Returns 0 on error.
 */
{
  static TXbool         fldcmpFailed = TXbool_False;
  size_t                rangeIdx;
  int	                compareVal, ret, status;

  for (rangeIdx = 0; rangeIdx < rangesInfo->numRanges; rangeIdx++)
    {                                           /* for each bin */
      TXsqlFuncLookup_Range     *range = rangesInfo->ranges[rangeIdx];

      /* Check if `keyItemFld' is within lower bound: */
      if (!(range->flags & TXsqlFuncLookup_Flag_InfiniteLower))
        {
          putfld(rangesInfo->boundItemFld, range->lowerBoundData,
                 range->lowerBoundLen);
          compareVal = TXfldCmpSameType(rangesInfo->keyItemFld,
                                        rangesInfo->boundItemFld, &status,
                                        OF_NORMAL);
          if (status != 0) goto badFldcmp;
          if (compareVal < 0 ||
              (compareVal == 0 &&
               !(range->flags & TXsqlFuncLookup_Flag_IncludeLower)))
            continue;                           /* `keyItemFld' too low */
        }

      /* Check if `keyItemFld' is within upper bound: */
      if (!(range->flags & TXsqlFuncLookup_Flag_InfiniteUpper))
        {
          putfld(rangesInfo->boundItemFld, range->upperBoundData,
                 range->upperBoundLen);
          compareVal = TXfldCmpSameType(rangesInfo->keyItemFld,
                                        rangesInfo->boundItemFld, &status,
                                        OF_NORMAL);
          if (status != 0)
            {
            badFldcmp:
              if (!fldcmpFailed)
                txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                              "TXfldCmpSameType() failed for types %s and %s",
                               TXfldtypestr(rangesInfo->keyItemFld),
                               TXfldtypestr(rangesInfo->boundItemFld));
              fldcmpFailed = TXbool_True;
              break;                            /* return ok but no bins */
            }
          if (compareVal > 0 ||
              (compareVal == 0 &&
               !(range->flags & TXsqlFuncLookup_Flag_IncludeUpper)))
            continue;                           /* `keyItemFld' too high */
        }

      /* `keyItemFld' matches `range'; add latter to return data: */
      if (!TXsqlFuncLookup_AppendResultItem(pmbuf, rangesInfo, namesInfo,
                                            range))
        goto err;
    }

  ret = 1;                                      /* success */
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_Range *
TXsqlFuncLookup_Range_Close(TXsqlFuncLookup_Range *range, FTN rangeItemType,
                            FTN boundItemType)
{
  if (!range) goto finally;

  TXftnFreeData(range->data, range->dataLen, rangeItemType, 1);
  range->data = NULL;
  TXftnFreeData(range->lowerBoundData, range->lowerBoundLen, boundItemType,
                1);
  range->lowerBoundData = NULL;
  TXftnFreeData(range->upperBoundData, range->upperBoundLen, boundItemType,
                1);
  range->upperBoundData = NULL;
  range->upperBoundSrc = TXfree(range->upperBoundSrc);
  range = TXfree(range);

finally:
  return(NULL);
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_Range *
TXsqlFuncLookup_Range_Open(TXPMBUF *pmbuf)
{
  TXsqlFuncLookup_Range *range;
  TXsqlFuncLookup_Part  partIdx;


  range = TX_NEW(pmbuf, TXsqlFuncLookup_Range);
  if (!range) goto err;
  for (partIdx = 0; partIdx < TXsqlFuncLookup_Part_NUM; partIdx++)
    range->partDataOff[partIdx] = range->partDataLen[partIdx] = -1;
  /* rest cleared by calloc(), and set by ...AddRange() */
  goto finally;

err:
  range = TXfree(range);
finally:
  return(range);
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_RangesInfo *
TXsqlFuncLookup_RangesInfo_Close(TXPMBUF *pmbuf,
                                 TXsqlFuncLookup_RangesInfo *rangesInfo)
{
  size_t        i;

  if (!rangesInfo) goto finally;

  if (rangesInfo->ranges)
    {
      for (i = 0; i < rangesInfo->numRanges; i++)
      rangesInfo->ranges[i]=TXsqlFuncLookup_Range_Close(rangesInfo->ranges[i],
                                     rangesInfo->rangeItemType,
                                     TXfldType(rangesInfo->boundItemFld));
      rangesInfo->ranges = TXfree(rangesInfo->ranges);
    }
  rangesInfo->keyItemFld = closefld(rangesInfo->keyItemFld);
  rangesInfo->boundItemFld = closefld(rangesInfo->boundItemFld);
  rangesInfo->returnStrlstBuf = closehtbuf(rangesInfo->returnStrlstBuf);
  rangesInfo->returnData = TXfree(rangesInfo->returnData);
  if (TXfldmathverb > 0 && rangesInfo)
    txpmbuf_putmsg(pmbuf, MINFO + TRACE_LOOKUP_OPENCLOSE, __FUNCTION__,
                   "Closed RangesInfo object %p", rangesInfo);
  rangesInfo = TXfree(rangesInfo);
finally:
  return(NULL);
}

/* ------------------------------------------------------------------------ */

static void
TXsqlFuncLookup_RangesInfo_Close_CookedWrapper(TXPMBUF *pmbuf, void *cooked)
/* Wrapper for TXftiValueWithCooked.  Closes cooked object, e.g. when
 * value is changed/closed.
 */
{
  TXsqlFuncLookup_RangesInfo_Close(pmbuf,
                                   (TXsqlFuncLookup_RangesInfo *)cooked);
}

/* ------------------------------------------------------------------------ */

static void *
TXsqlFuncLookup_ConvertBoundToKeyItemType(TXPMBUF *pmbuf,
    const char *sqlFuncName, const char *bound, size_t boundLen,
    FLD *rangeItemFld, TXsqlFuncLookup_RangesInfo *rangesInfo, FLDOP *fldop,
    size_t *resLen)
/* Convert `bound' to key item type.  `rangeItemFld', `fldop' are scratch.
 * Returns FTN-alloced key-item type data, setting `*resLen'.
 */
{
  static const char     whitespace[] = " \t\r\n\v\f";
  FLD                   *resFld = NULL;
  void                  *ret;
  char                  *e;
  int                   errnum;
  const char            *boundEnd = bound + boundLen;

  /* We are not nul-terminating at `boundLen' (not sure if data is
   * writable), and fldmath's varchar-to-num conversion is lax.  So do
   * typical numeric conversion ourselves:
   */
  switch (TXfldbasetype(rangesInfo->keyItemFld))
    {
#define INTEGRAL_CASE(ftn, type, strToType)                             \
      case (ftn):                                                       \
        ret = (type *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(type));   \
        if (!ret) goto err;                                             \
        *(type *)ret = (strToType)(bound, boundEnd, &e, 0, &errnum);    \
        /* Skip `e' past any trailing whitespace: */                    \
        e += TXstrspnBuf(e, boundEnd, whitespace, sizeof(whitespace) - 1); \
        if (e < boundEnd || errnum) goto badVal;                        \
        *resLen = 1;                                                    \
        break
    INTEGRAL_CASE(FTN_INT, ft_int, TXstrtoi32);
    INTEGRAL_CASE(FTN_INTEGER, ft_int, TXstrtoi32);
    INTEGRAL_CASE(FTN_LONG, ft_long, TXstrtol);
    INTEGRAL_CASE(FTN_SHORT, ft_short, TXstrtos);
    INTEGRAL_CASE(FTN_SMALLINT, ft_smallint, TXstrtos);
    INTEGRAL_CASE(FTN_WORD, ft_word, TXstrtow);
    INTEGRAL_CASE(FTN_DWORD, ft_dword, TXstrtodw);
    INTEGRAL_CASE(FTN_INT64, ft_int64, TXstrtoi64);
    INTEGRAL_CASE(FTN_UINT64, ft_uint64, TXstrtoui64);
    case FTN_FLOAT:
      ret = (ft_float *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(ft_float));
      if (!ret) goto err;
      *(ft_float *)ret = (ft_float)TXstrtod(bound, boundEnd, &e, &errnum);
      e += TXstrspnBuf(e, boundEnd, whitespace, sizeof(whitespace) - 1);
      if (e < boundEnd || errnum) goto badVal;
      *resLen = 1;
      break;
    case FTN_DOUBLE:
      ret = (ft_double *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(ft_double));
      if (!ret) goto err;
      *(ft_double *)ret = TXstrtod(bound, boundEnd, &e, &errnum);
      e += TXstrspnBuf(e, boundEnd, whitespace, sizeof(whitespace) - 1);
      if (e < boundEnd || errnum)
        {
        badVal:
          txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
                     "Invalid %s() range boundary `%.*s': Cannot parse as %s",
                         sqlFuncName, (int)boundLen, bound,
                         ddfttypename(TXfldbasetype(rangesInfo->keyItemFld)));
          goto err;
        }
      *resLen = 1;
      break;
    default:
      putfldinit(rangeItemFld, (char *)bound, boundLen);
      if (fopush(fldop, rangeItemFld) != 0 ||
          fopush(fldop, rangesInfo->keyItemFld) != 0)
        goto err;
      if (foop(fldop, FOP_CNV) != 0 ||
          !(resFld = fopop(fldop)) ||
          (TXfldType(resFld) & FTN_VarBaseTypeMask) !=
          (TXfldType(rangesInfo->boundItemFld) & FTN_VarBaseTypeMask))
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
                         "Cannot convert %s() range boundary `%.*s' to %s",
                         sqlFuncName, (int)boundLen, bound,
                         ddfttypename(TXfldbasetype(rangesInfo->keyItemFld)));
          goto err;
        }
      ret = TXfldTakeAllocedData(resFld, resLen);
      break;
    }
  goto finally;

err:
  ret = NULL;
  *resLen = 0;
finally:
  resFld = closefld(resFld);
  return(ret);
#undef INTEGRAL_CASE
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_Range *
TXsqlFuncLookup_ParseAndAddRange(TXPMBUF *pmbuf, const char *sqlFuncName,
                                 TXsqlFuncLookup_RangesInfo *rangesInfo,
    FLD *rangeItemFld, FLDOP *fldop, void *rangeItemData, size_t rangeItemLen)
/* Parses and adds `rangeItemData' as next bin.  Caller must set range's
 * `orgBinIdx' and update `rangesInfo->numOrgBins'.
 * `rangeItemFld' is a scratch field of the appropriate (range item) type.
 * `fldop' is a scratch FLDOP.
 * Returns new range, TXsqlFuncLookup_Range_INVALID if invalid range,
 * or NULL on severe error.  Note that returned range may be deleted
 * on a future call, e.g. if implicit upper bound and later range makes it
 * illegal.
 */
{
  static const char     rangeIgnored[] = "range ignored";
  FLD                   *resFld = NULL;
  const char            *rangeItemDataEnd, *dotDot;
  TXsqlFuncLookup_Range *range = NULL, *prevRange = NULL;

  /* Make room for new bin: */
  if (!TX_INC_ARRAY(pmbuf, &rangesInfo->ranges, rangesInfo->numRanges,
                    &rangesInfo->numAllocedRanges))
    goto err;
  range = TXsqlFuncLookup_Range_Open(pmbuf);
  if (!range) goto err;
  prevRange = (rangesInfo->numRanges > 0 ?
               rangesInfo->ranges[rangesInfo->numRanges - 1] : NULL);

  /* Save a copy of range items, in case we are returning them
   * (i.e. no `namesFld'):
   */
  range->data = TXftnDupData(rangeItemData, rangeItemLen,
                             rangesInfo->rangeItemType,
                             rangeItemLen*ddftsize(rangesInfo->rangeItemType),
                             NULL);
  if (!range->data) goto err;
  range->dataLen = rangeItemLen;

  /* Each range is either an explicit range in `..' form, or an
   * implicit range (a single value inclusive lower bound, with
   * implicit exclusive upper bound being the next range's lower
   * bound).
   */
  rangeItemDataEnd = (const char *)rangeItemData + rangeItemLen;
  if (TXfldbasetype(rangeItemFld) == FTN_CHAR)
    {
      for (dotDot = (const char *)rangeItemData;
           dotDot < rangeItemDataEnd - 1;
           dotDot++)
        if (*dotDot == '.' && dotDot[1] == '.')
          break;
    }
  else
    dotDot = rangeItemDataEnd;
  if (dotDot < rangeItemDataEnd - 1)            /* explicit range */
    {
      TXbool            sawLowerFlag = TXbool_False;
      const char        *s, *e;

      range->partDataOff[TXsqlFuncLookup_Part_RangeOperator] =
        dotDot - (const char *)rangeItemData;
      range->partDataLen[TXsqlFuncLookup_Part_RangeOperator] = 2;

      /* Parse a range of the form:
       *   [lowerBound..upperBound]
       * Square bracket(s) are optional, and indicate inclusivity.
       * Curly brace(s) indicate exclusivity.  Either bound is optional
       * and is infinite if not present.  Since booundaries are compared
       * to the key item, we simply cast each boundary to key item type.
       */
      /* Default is to include lower bound but not upper, so e.g.:
       *   10..20,20..30,30..40
       * is continguous and non-overlapping, and consistent with:
       *   10,20,30,40
       * which includes lower bound but not (implicit) upper bound
       */
      range->flags = TXsqlFuncLookup_Flag_IncludeLower;
      /* Check for lower bound inclusivity: */
      s = (const char *)rangeItemData;
      e = dotDot;
      if (s < e)
        switch (*s)
          {
          case '[':
            range->flags |= TXsqlFuncLookup_Flag_IncludeLower;
            sawLowerFlag = TXbool_True;
            range->partDataOff[TXsqlFuncLookup_Part_LowerInclusivity] =
              s - (const char *)rangeItemData;
            range->partDataLen[TXsqlFuncLookup_Part_LowerInclusivity] = 1;
            s++;
            break;
          case '{':
            range->flags &= ~TXsqlFuncLookup_Flag_IncludeLower;
            sawLowerFlag = TXbool_True;
            range->partDataOff[TXsqlFuncLookup_Part_LowerInclusivity] =
              s - (const char *)rangeItemData;
            range->partDataLen[TXsqlFuncLookup_Part_LowerInclusivity] = 1;
            s++;
            break;
          case ']':
          case '}':
            goto badSyntax;
          }
      /* Convert and copy the lower bound, if any: */
      if (s < e)                                /* have a lower bound */
        {
          range->lowerBoundData =
            TXsqlFuncLookup_ConvertBoundToKeyItemType(pmbuf, sqlFuncName, s,
                                                      e - s, rangeItemFld,
                                                      rangesInfo, fldop,
                                                      &range->lowerBoundLen);
          if (!range->lowerBoundData)
            {
              txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
             "Skipping %s() range `%.*s' due to above error with lower bound",
                             sqlFuncName, (int)rangeItemLen,
                             (char *)rangeItemData);
              goto skipBin;
            }
          range->partDataOff[TXsqlFuncLookup_Part_LowerBound] =
            s - (const char *)rangeItemData;
          range->partDataLen[TXsqlFuncLookup_Part_LowerBound] = e - s;
        }
      else                                      /* no lower bound */
        range->flags |= TXsqlFuncLookup_Flag_InfiniteLower;
      /* Check for upper bound inclusivity: */
      s = dotDot + 2;                           /* just past `..' */
      e = rangeItemDataEnd;
      if (s < e)
        switch (e[-1])
          {
          case ']':
            if (!sawLowerFlag) goto badSyntax;
            range->flags |= TXsqlFuncLookup_Flag_IncludeUpper;
            range->partDataOff[TXsqlFuncLookup_Part_UpperInclusivity] =
              (e - 1) - (const char *)rangeItemData;
            range->partDataLen[TXsqlFuncLookup_Part_UpperInclusivity] = 1;
            e--;
            break;
          case '}':
            if (!sawLowerFlag) goto badSyntax;
            range->flags &= ~TXsqlFuncLookup_Flag_IncludeUpper;
            range->partDataOff[TXsqlFuncLookup_Part_UpperInclusivity] =
              (e - 1) - (const char *)rangeItemData;
            range->partDataLen[TXsqlFuncLookup_Part_UpperInclusivity] = 1;
            e--;
            break;
          case '[':
          case '{':
            goto badSyntax;
          default:
            /* For consistency, brackets must be paired, even though
             * as a pair they are optional.  It is more likely that
             * `[X..Y' is a typo than `I want default upper inclusivity':
             */
            if (sawLowerFlag)
              {
              badSyntax:
                txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
                               "Invalid syntax in %s() range `%.*s'; %s",
                               sqlFuncName, (int)rangeItemLen,
                               (char *)rangeItemData, rangeIgnored);
                goto skipBin;
              }
            break;
          }
      /* Convert and copy the upper bound, if any: */
      if (s < e)                                /* have an upper bound */
        {
          range->upperBoundData =
            TXsqlFuncLookup_ConvertBoundToKeyItemType(pmbuf, sqlFuncName, s,
                                                      e - s, rangeItemFld,
                                                      rangesInfo, fldop,
                                                      &range->upperBoundLen);
          if (!range->upperBoundData)
            {
              txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
             "Skipping %s() range `%.*s' due to above error with upper bound",
                             sqlFuncName, (int)rangeItemLen,
                             (char *)rangeItemData);
              goto skipBin;
            }
          range->partDataOff[TXsqlFuncLookup_Part_UpperBound] =
            s - (const char *)rangeItemData;
          range->partDataLen[TXsqlFuncLookup_Part_UpperBound] = e - s;
        }
      else                                      /* no upper bound */
        range->flags |= TXsqlFuncLookup_Flag_InfiniteUpper;
      /* Ensure range is valid: */
      if (!(range->flags & (TXsqlFuncLookup_Flag_InfiniteLower |
                            TXsqlFuncLookup_Flag_InfiniteUpper)))
        {                                       /* both bounds non-infinite */
          int           status, compareVal;
          const char    *reason;

          putfld(rangesInfo->keyItemFld, range->lowerBoundData,
                 range->lowerBoundLen);
          putfld(rangesInfo->boundItemFld, range->upperBoundData,
                 range->lowerBoundLen);
          compareVal = TXfldCmpSameType(rangesInfo->keyItemFld,
                                        rangesInfo->boundItemFld, &status,
                                        OF_NORMAL);
          if (status != 0)                      /* bad field compare */
            reason = "Cannot compare bounds";
          else if (compareVal > 0 ||            /* lower > upper */
                   (compareVal == 0 &&          /* lower == upper and */
                    (range->flags &             /*   either exclusive */
                     (TXsqlFuncLookup_Flag_IncludeLower |
                      TXsqlFuncLookup_Flag_IncludeUpper)) !=
                    (TXsqlFuncLookup_Flag_IncludeLower |
                     TXsqlFuncLookup_Flag_IncludeUpper)))
            reason = "Lower bound exceeds upper bound";
          else
            reason = NULL;
          if (reason)
            {
              txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                             "Illegal %s() range `%.*s': %s; %s",
                             sqlFuncName, (int)rangeItemLen,
                             (char *)rangeItemData, reason, rangeIgnored);
              goto skipBin;
            }
        }
    }
  else                                          /* implicit range (1 value) */
    {
      /* Cast the (single) range value to `keyItemFld' type.
       * Use ..._ConvertBound... if possible, as simple FOP_CNV of
       * e.g. `foo10' to int will not yap on bad data:
       */
      if (TXfldbasetype(rangeItemFld) == FTN_CHAR)
        {
          range->lowerBoundData =
            TXsqlFuncLookup_ConvertBoundToKeyItemType(pmbuf, sqlFuncName,
                                rangeItemData, rangeItemLen, rangeItemFld,
                                rangesInfo, fldop, &range->lowerBoundLen);
          range->partDataOff[TXsqlFuncLookup_Part_LowerBound] = 0;
          range->partDataLen[TXsqlFuncLookup_Part_LowerBound] = rangeItemLen;
        }
      else                                      /* use FOP_CNV */
        {
          putfldinit(rangeItemFld, rangeItemData, rangeItemLen);
          if (fopush(fldop, rangeItemFld) != 0 ||
              fopush(fldop, rangesInfo->keyItemFld) != 0)
            goto err;
          if (foop(fldop, FOP_CNV) != 0 ||
              !(resFld = fopop(fldop)) ||
              (TXfldType(resFld) & FTN_VarBaseTypeMask) !=
              (TXfldType(rangesInfo->boundItemFld) & FTN_VarBaseTypeMask))
            {
              txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                             "Unexpected type %s converting range item type %s to key item type %s for range `%.*s'; %s",
                             TXfldtypestr(resFld), TXfldtypestr(rangeItemFld),
                             TXfldtypestr(rangesInfo->keyItemFld),
                             (int)rangeItemLen, (char *)rangeItemData,
                             rangeIgnored);
              goto skipBin;
            }
          range->lowerBoundData = TXfldTakeAllocedData(resFld,
                                                       &range->lowerBoundLen);
          resFld = closefld(resFld);
        }
      /* For each bound[i] we generate a range:
       *   [ bound[i] .. bound[i+1] }
       * Last one is:
       *   [ bound[i] .. ]
       *
       * Unlike pre-Bug 6974, we will not lump values less than
       * bound[0] into the first bin, because they do not belong in
       * that range.  They are discarded, just like values not in any
       * other bin (which is possible now that Bug 6974 allows
       * explicit and thus non-contiguous ranges).  Discarding is
       * possible (unlike pre-Bug 6974) because our return type is
       * always var+multi, and thus zero items may be returned.
       */
      if (!range->lowerBoundData) goto err;
      range->flags = (TXsqlFuncLookup_Flag_IncludeLower |
                      TXsqlFuncLookup_Flag_IncludeUpper |
                      TXsqlFuncLookup_Flag_InfiniteUpper |
                      TXsqlFuncLookup_Flag_ImplicitUpper);
      range->upperBoundData = NULL;
      range->upperBoundLen = 0;
    }

  /* Fix up the previous bin's upper boundary, if it was implicitly
   * the next (i.e. current) bin's lower boundary:
   */
  if (prevRange &&
      (prevRange->flags & TXsqlFuncLookup_Flag_ImplicitUpper))
    {
      TXftnFreeData(prevRange->upperBoundData, prevRange->upperBoundLen,
                    TXfldType(rangesInfo->boundItemFld), 1);
      prevRange->upperBoundData =
        TXftnDupData(range->lowerBoundData, range->lowerBoundLen,
                     TXfldType(rangesInfo->boundItemFld),
                  range->lowerBoundLen*TXfldGetElsz(rangesInfo->boundItemFld),
                     NULL);
      if (!prevRange->upperBoundData) goto err;
      prevRange->upperBoundLen = range->lowerBoundLen;

      prevRange->upperBoundSrc = TXfree(prevRange->upperBoundSrc);
      if (range->partDataOff[TXsqlFuncLookup_Part_LowerBound]!=(size_t)(-1) &&
          range->partDataLen[TXsqlFuncLookup_Part_LowerBound] != (size_t)(-1))
        prevRange->upperBoundSrc = TXstrndup(pmbuf, __FUNCTION__,
                                             (char *)range->data +
                        range->partDataOff[TXsqlFuncLookup_Part_LowerBound],
                        range->partDataLen[TXsqlFuncLookup_Part_LowerBound]);

      prevRange->flags &= ~(TXsqlFuncLookup_Flag_IncludeUpper |
                            TXsqlFuncLookup_Flag_InfiniteUpper);
      /* Ensure previous range is still valid: */
      if (!(prevRange->flags & (TXsqlFuncLookup_Flag_InfiniteLower |
                                TXsqlFuncLookup_Flag_InfiniteUpper)))
        {                                       /* both bounds non-infinite */
          int           status, compareVal;
          const char    *reason;

          putfld(rangesInfo->keyItemFld, prevRange->lowerBoundData,
                 prevRange->lowerBoundLen);
          putfld(rangesInfo->boundItemFld, prevRange->upperBoundData,
                 prevRange->lowerBoundLen);
          compareVal = TXfldCmpSameType(rangesInfo->keyItemFld,
                                        rangesInfo->boundItemFld, &status,
                                        OF_NORMAL);
          if (status != 0)                      /* bad field compare */
            reason = "Cannot compare with implicit upper bound";
          else if (compareVal > 0 ||            /* lower > upper */
                   (compareVal == 0 &&          /* lower == upper and */
                    (prevRange->flags &         /*   either exclusive */
                     (TXsqlFuncLookup_Flag_IncludeLower |
                      TXsqlFuncLookup_Flag_IncludeUpper)) !=
                    (TXsqlFuncLookup_Flag_IncludeLower |
                     TXsqlFuncLookup_Flag_IncludeUpper)))
            reason = "Exceeds implicit upper bound";
          else
            reason = NULL;
          if (reason)
            {
              char      *prevRangeStr;
              size_t    prevRangeStrLen;

              if ((rangesInfo->rangeItemType & DDTYPEBITS) == FTN_CHAR)
                {
                  prevRangeStr = prevRange->data;
                  prevRangeStrLen = prevRange->dataLen;
                }
              else
                {
                  prevRangeStr = fldtostr(rangesInfo->keyItemFld);
                  prevRangeStrLen = strlen(prevRangeStr);
                }
              txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Illegal %s() range lower bound `%.*s': %s `%s'; %s",
                             sqlFuncName, (int)prevRangeStrLen, prevRangeStr,
                             reason, fldtostr(rangesInfo->boundItemFld),
                             rangeIgnored);
              rangesInfo->ranges[--rangesInfo->numRanges] = prevRange =
             TXsqlFuncLookup_Range_Close(prevRange,rangesInfo->rangeItemType,
                                         TXfldType(rangesInfo->boundItemFld));
            }
        }
    }

  rangesInfo->ranges[rangesInfo->numRanges++] = range;
  goto finally;

skipBin:
  range = TXsqlFuncLookup_Range_Close(range, rangesInfo->rangeItemType,
                                      TXfldType(rangesInfo->boundItemFld));
  range = TXsqlFuncLookup_Range_INVALID;
  goto finally;
err:
  range = TXsqlFuncLookup_Range_Close(range, rangesInfo->rangeItemType,
                                      TXfldType(rangesInfo->boundItemFld));
finally:
  resFld = closefld(resFld);
  return(range);
}

/* ------------------------------------------------------------------------ */

static void *
TXsqlFuncLookup_RangesInfo_PrintRanges(TXPMBUF *pmbuf,
        TXsqlFuncLookup_RangesInfo *rangesInfo, TXbool srcBounds,
        TXbool asStrlst, size_t *len)
/* Prints ranges of `rangesInfo' to an alloced string (or strlst).
 * Prints source bounds if `srcBounds', else cooked (converted to key type).
 * Returns alloced string or strlst, or NULL on error; sets `*len' to byte len.
 */
{
  void          *ret;
  HTBUF         *buf = NULL;
  size_t        i, retLen;

  if (!(buf = openhtbuf()) ||
      (asStrlst && !TXstrlstBufBegin(buf)))
    goto err;
  htbuf_setpmbuf(buf, pmbuf, 0x3);

  for (i = 0; i < rangesInfo->numRanges; i++)
    {
      TXsqlFuncLookup_Range     *range = rangesInfo->ranges[i];

      if (i > 0 && !asStrlst) htbuf_pf(buf, ",");
      if (range->flags & TXsqlFuncLookup_Flag_IncludeLower)
        htbuf_pf(buf, "[");
      else
        htbuf_pf(buf, "{");
      if (range->flags & TXsqlFuncLookup_Flag_InfiniteLower)
        ;
      else if (srcBounds &&
        range->partDataOff[TXsqlFuncLookup_Part_LowerBound] != (size_t)(-1) &&
        range->partDataLen[TXsqlFuncLookup_Part_LowerBound] != (size_t)(-1))
        {
          htbuf_pf(buf, "%.*s",
                   (int)range->partDataLen[TXsqlFuncLookup_Part_LowerBound],
                   (char *)range->data +
                   range->partDataOff[TXsqlFuncLookup_Part_LowerBound]);
        }
      else
        {
          putfld(rangesInfo->boundItemFld, range->lowerBoundData,
                 range->lowerBoundLen);
          htbuf_pf(buf, "%s", fldtostr(rangesInfo->boundItemFld));
        }
      htbuf_pf(buf, "..");
      if (range->flags & TXsqlFuncLookup_Flag_InfiniteUpper)
        ;
      else if (srcBounds &&
        range->partDataOff[TXsqlFuncLookup_Part_UpperBound] != (size_t)(-1) &&
        range->partDataLen[TXsqlFuncLookup_Part_UpperBound] != (size_t)(-1))
        {
          htbuf_pf(buf, "%.*s",
                   (int)range->partDataLen[TXsqlFuncLookup_Part_UpperBound],
                   (char *)range->data +
                   range->partDataOff[TXsqlFuncLookup_Part_UpperBound]);
        }
      else if (srcBounds && range->upperBoundSrc)
        htbuf_pf(buf, "%s", range->upperBoundSrc);
      else
        {
          putfld(rangesInfo->boundItemFld, range->upperBoundData,
                 range->upperBoundLen);
          htbuf_pf(buf, "%s", fldtostr(rangesInfo->boundItemFld));
        }
      if (range->flags & TXsqlFuncLookup_Flag_IncludeUpper)
        htbuf_pf(buf, "]");
      else
        htbuf_pf(buf, "}");
      if (asStrlst) htbuf_write(buf, "", 1);
    }

  /* Terminate buffer: */
  if (asStrlst)
    {
      if (!TXstrlstBufEnd(buf)) goto err;
    }
  else if (rangesInfo->numRanges == 0)
    htbuf_write(buf, "", 1);

  if (htbuf_getflags(buf, HTBF_ERROR)) goto err;
  retLen = htbuf_getdata(buf, (char **)(void *)&ret, 1);
  if (len) *len = retLen;
  goto finally;

err:
  ret = NULL;
  if (len) *len = 0;
finally:
  buf = closehtbuf(buf);
  return(ret);
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_RangesInfo *
TXsqlFuncLookup_RangesInfo_Open(TXPMBUF *pmbuf, const char *sqlFuncName,
                FLD *keysFld, FLD *rangesFld, FLD *namesFld, FLDOP *fldop)
/* Parses `rangesFld' and creates a bin ranges object for quick lookup.
 * `fldop' is a scratch FLDOP to use.  `namesFld' is optional.
 */
{
  TXsqlFuncLookup_RangesInfo    *rangesInfo = NULL;
  void                          *rangeItemData, *rangePrevItemData;
  size_t                        rangeItemLen, rangePrevItemLen;
  FLD                           rangeItemFld;
  TXbool                        keysFldIsMultiItemType;
  FTN                           keyItemType;

  if (!(rangesInfo = TX_NEW(pmbuf, TXsqlFuncLookup_RangesInfo))) goto err;

  TXfldIsMultipleItemType(rangesFld, &rangesInfo->rangesType,
                          &rangesInfo->rangeItemType);
  if (!initfld(&rangeItemFld, rangesInfo->rangeItemType, 1)) goto err;
  /* `keyItemFld' will hold each `keysFld' item during lookup: */
  keysFldIsMultiItemType =
    (TXbool)TXfldIsMultipleItemType(keysFld, &rangesInfo->keysType,
                                    &keyItemType);
  rangesInfo->keyItemFld = createfld(ddfttypename(keyItemType), 1, 1);
  if (!rangesInfo->keyItemFld) goto err;
  /* `boundItemFld' will hold each boundary item during lookup.  Must
   * be same type as `keyItemFld', for TXsameTypeFldCmp().  Also makes
   * sense since `keyItemType' is what we are looking up, so its type
   * should drive the comparisons.  This also lets us know what type
   * the (probably varchar) boundary values should be parsed as:
   */
  rangesInfo->boundItemFld = newfld(rangesInfo->keyItemFld);
  if (!rangesInfo->boundItemFld) goto err;

  /* Get return-item, and overall, type and size (must agree with
   * ...GetReturnType()).  Also prep for multi-item return, which
   * after Bug 6974 is (nearly) always:
   */
  if (namesFld)                                 /* we return a bin *name* */
    {
      TXfldIsMultipleItemType(namesFld, &rangesInfo->namesType,
                              &rangesInfo->nameItemType);
      rangesInfo->returnItemType = rangesInfo->nameItemType;
      if ((rangesInfo->namesType & DDTYPEBITS) == FTN_CHAR &&  /*single-item*/
          keysFldIsMultiItemType)
        rangesInfo->returnType = FTN_varSTRLST;
      else
        rangesInfo->returnType = (rangesInfo->namesType | DDVARBIT);
    }
  else                                          /* we return a bin *range* */
    {
      rangesInfo->namesType = rangesInfo->nameItemType = (FTN)0;
      rangesInfo->returnItemType = rangesInfo->rangeItemType;
      if ((rangesInfo->rangesType & DDTYPEBITS) == FTN_CHAR && /*single-item*/
          keysFldIsMultiItemType)
        rangesInfo->returnType = FTN_varSTRLST;
      else
        rangesInfo->returnType = (rangesInfo->rangesType | DDVARBIT);
    }
  rangesInfo->returnItemElSz = ddftsize(rangesInfo->returnItemType);
  if ((rangesInfo->returnType & DDTYPEBITS) == FTN_STRLST &&
      !(rangesInfo->returnStrlstBuf = openhtbuf()))
    goto err;

  /* Iterate over bins and parse them into bounds and flags: */
  rangeItemData = rangePrevItemData = NULL;
  rangeItemLen = rangePrevItemLen = 0;
  for (rangesInfo->numOrgBins = 0;
       (rangeItemData = TXfldGetNextItem(rangesFld, rangePrevItemData,
                                    rangePrevItemLen, &rangeItemLen)) != NULL;
       rangePrevItemData = rangeItemData, rangePrevItemLen = rangeItemLen,
         rangesInfo->numOrgBins++)
    {                                           /* each `rangesFld' item */
      TXsqlFuncLookup_Range     *range;

      range = TXsqlFuncLookup_ParseAndAddRange(pmbuf, sqlFuncName, rangesInfo,
                                               &rangeItemFld, fldop,
                                               rangeItemData, rangeItemLen);
      if (!range) goto err;                     /* severe error */
      if (range != TXsqlFuncLookup_Range_INVALID)
        range->orgBinIdx = rangesInfo->numOrgBins;
    }

  goto finally;

err:
  rangesInfo = TXsqlFuncLookup_RangesInfo_Close(pmbuf, rangesInfo);
finally:
  if (TXfldmathverb > 0 && rangesInfo)
    {
      char      *rangesStr;

      rangesStr = TXsqlFuncLookup_RangesInfo_PrintRanges(pmbuf, rangesInfo,
                                           TXbool_False, TXbool_False, NULL);
      txpmbuf_putmsg(pmbuf, MINFO + TRACE_LOOKUP_OPENCLOSE, __FUNCTION__,
                     "Opened TXsqlFuncLookup_RangesInfo object %p with return type %s: from keys type %s, %wd range%s (%s) from %s list `%s', and %s names list %s%s%s",
                     rangesInfo, ddfttypename(rangesInfo->returnType),
                     TXfldtypestr(keysFld),
                     (EPI_HUGEINT)rangesInfo->numRanges,
                     (rangesInfo->numRanges == 1 ? "" : "s"), rangesStr,
                     TXfldtypestr(rangesFld), fldtostr(rangesFld),
                     (namesFld ? TXfldtypestr(namesFld) : "NULL"),
                     (namesFld ? "`" : ""),
                     (namesFld ? fldtostr(namesFld) : "NULL"),
                     (namesFld ? "'" : ""));
      rangesStr = TXfree(rangesStr);
    }
  return(rangesInfo);
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_NamesInfo *
TXsqlFuncLookup_NamesInfo_Close(TXPMBUF *pmbuf,
                                TXsqlFuncLookup_NamesInfo *namesInfo)
{
  size_t        i;

  if (!namesInfo) goto finally;

  for (i = 0; i < namesInfo->numNames; i++)
    {
      if (namesInfo->nameDatas && namesInfo->nameLens)
        {
          TXftnFreeData(namesInfo->nameDatas[i], namesInfo->nameLens[i],
                        namesInfo->nameItemType, 1);
          namesInfo->nameDatas[i] = NULL;
        }
    }
  namesInfo->nameDatas = TXfree(namesInfo->nameDatas);
  namesInfo->nameLens = TXfree(namesInfo->nameLens);
  if (TXfldmathverb > 0 && namesInfo)
    txpmbuf_putmsg(pmbuf, MINFO + TRACE_LOOKUP_OPENCLOSE, __FUNCTION__,
                   "Closed NamesInfo object %p", namesInfo);
  namesInfo = TXfree(namesInfo);
finally:
  return(NULL);
}

/* ------------------------------------------------------------------------ */

static void
TXsqlFuncLookup_NamesInfo_Close_CookedWrapper(TXPMBUF *pmbuf, void *cooked)
/* Wrapper for TXftiValueWithCooked.  Closes payload, e.g. when value
 * is changed/closed.
 */
{
  TXsqlFuncLookup_NamesInfo_Close(pmbuf, (TXsqlFuncLookup_NamesInfo *)cooked);
}

/* ------------------------------------------------------------------------ */

static TXsqlFuncLookup_NamesInfo *
TXsqlFuncLookup_NamesInfo_Open(TXPMBUF *pmbuf, FLD *namesFld)
/* Parses `namesFld' and creates a bin names object for quick lookup.
 */
{
  TXsqlFuncLookup_NamesInfo     *namesInfo = NULL;
  size_t                        numAllocedNameDatas = 0;
  size_t                        numAllocedNameLens = 0;
  void                          *nameItemData, *namePrevItemData;
  size_t                        nameItemLen, namePrevItemLen;
  size_t                        nameItemElSz;

  if (!(namesInfo = TX_NEW(pmbuf, TXsqlFuncLookup_NamesInfo))) goto err;
  
  TXfldIsMultipleItemType(namesFld, &namesInfo->namesType,
                          &namesInfo->nameItemType);
  nameItemElSz = ddftsize(namesInfo->nameItemType);

  /* For quick lookup, split and dup `namesFld' into array
   * `namesInfo->nameDatas':
   */
  nameItemData = namePrevItemData = NULL;
  nameItemLen = namePrevItemLen = 0;
  for (namesInfo->numNames = 0;
       (nameItemData = TXfldGetNextItem(namesFld, namePrevItemData,
                                namePrevItemLen, &nameItemLen)) != NULL;
       namePrevItemData = nameItemData,
         namePrevItemLen = nameItemLen, namesInfo->numNames++)
    {                                           /* each `namesFld' item */
      if (!TX_INC_ARRAY(pmbuf, &namesInfo->nameDatas, namesInfo->numNames,
                        &numAllocedNameDatas) ||
          !TX_INC_ARRAY(pmbuf, &namesInfo->nameLens, namesInfo->numNames,
                        &numAllocedNameLens))
        goto err;
      /* Dup the item.  Could just point into `namesFld' data,
       * since we are a cache object that is predicated on
       * `namesFld' data not changing for our lifetime.  But be
       * safe and dup:
       */
      namesInfo->nameDatas[namesInfo->numNames] =
        TXftnDupData(nameItemData, nameItemLen, namesInfo->nameItemType,
                     nameItemLen*nameItemElSz, NULL);
      if (!namesInfo->nameDatas[namesInfo->numNames]) goto err;
      namesInfo->nameLens[namesInfo->numNames] = nameItemLen;
    }

  goto finally;

err:
  namesInfo = TXsqlFuncLookup_NamesInfo_Close(pmbuf, namesInfo);
finally:
  if (TXfldmathverb > 0 && namesInfo)
    txpmbuf_putmsg(pmbuf, MINFO + TRACE_LOOKUP_OPENCLOSE, __FUNCTION__,
"Opened TXsqlFuncLookup_NamesInfo object %p with %wd names from %s list `%s'",
                   namesInfo, (EPI_HUGEINT)namesInfo->numNames,
                   TXfldtypestr(namesFld), fldtostr(namesFld));
  return(namesInfo);
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_lookup(FLD *keysFld, FLD *rangesFld, FLD *namesFld)
/* SQL lookup(keysFld, rangesFld[, namesFld]).
 * `rangesFld' values are each either a range of the form:
 *
 *   [lowerBound..upperBound]
 *
 * or a single inclusive lower bound value (upper exclusive bound is
 * next value, or no limit if last value).  This function SQL-returns
 * each `rangesFld' item (or corresponding `namesFld' item, if
 * `namesFld' given) that `keysFld' is within.  If `keysFld' is
 * multi-value, looks up those multiple values.  NOTE: SQL return type
 * must correspond with TXsqlFuncLookupReturnType().
 * Returns FOP_EOK on success, else FOP_E... error.
 */
{
  static const char                     sqlFuncName[] = "lookup";
  TXPMBUF                               *pmbuf = NULL;
  FLDOP                                 *fldop = NULL;
  int                                   ret, res;
  size_t                                keyItemIdx;
  FTN                                   keysType, keyItemType;
  void                                  *keyItemData, *returnData = NULL;
  size_t                                keyItemLen;
  size_t                                returnLen = 0;
  TXsqlFuncLookup_RangesInfo            *rangesInfo = NULL;
  TXbool                                closeRangesInfo = TXbool_False;
  TXsqlFuncLookup_NamesInfo             *namesInfo = NULL;
  TXbool                                closeNamesInfo = TXbool_False;
  char                                  *keysDup = NULL;
  ft_internal                           *fti;
  TXftiValueWithCooked                  *rangesValueWithCooked = NULL;

  if (!(fldop = TXgetFldopFromCache())) goto err;

  /* Keys may be multi-value; we deal with an item at a time: */
  TXfldIsMultipleItemType(keysFld, &keysType, &keyItemType);

  /* We need a ...RangesInfo object, which is the parsed ranges of
   * `rangesFld' for TXsqlFuncLookup_FindKeyItemAndAppendBins()
   * lookup.  Optimization: see if `rangesFld' has a
   * TXftiValueWithCooked that already contains such an object, from a
   * previous call:
   */
  if ((TXfldType(rangesFld) & DDTYPEBITS) == FTN_INTERNAL &&
      (fti = getfld(rangesFld, NULL)) != NULL &&
      TX_FTI_VALID(fti) &&
      (rangesValueWithCooked = tx_fti_getobj(fti)) != NULL)
    {
      FTN               rangesType, rangeItemType, namesType, nameItemType;
      void                                      *cooked;
      TXftiValueWithCooked_CloseCookedFunc      *closeCooked;

      /* Prep for verify: */
      TXfldIsMultipleItemType(rangesFld, &rangesType, &rangeItemType);
      if (namesFld)
        TXfldIsMultipleItemType(namesFld, &namesType, &nameItemType);
      else
        namesType = nameItemType = (FTN)0;
      /* Verify and use cooked obj if possible: */
      if (TXftiValueWithCooked_GetCookedAndCloseFunc(rangesValueWithCooked,
                                                     &cooked, &closeCooked) &&
          closeCooked == TXsqlFuncLookup_RangesInfo_Close_CookedWrapper &&
          (rangesInfo = (TXsqlFuncLookup_RangesInfo *)cooked) != NULL &&
          /* Return type, return item type, etc. were determined in
           * ...RangesInfo_Open(), and depend on key, ranges *and* names types:
           */
          /* keys type: */
          (rangesInfo->keysType & FTN_VarBaseTypeMask) ==
          (keysType & FTN_VarBaseTypeMask) &&
          (TXfldType(rangesInfo->keyItemFld) & FTN_VarBaseTypeMask) ==
          (keyItemType & FTN_VarBaseTypeMask) &&
          /* ranges types: */
          (rangesInfo->rangesType & FTN_VarBaseTypeMask) ==
          (rangesType & FTN_VarBaseTypeMask) &&
          (rangesInfo->rangeItemType & FTN_VarBaseTypeMask) ==
          (rangeItemType & FTN_VarBaseTypeMask) &&
          /* names types: */
          (rangesInfo->namesType & FTN_VarBaseTypeMask) ==
          (namesType & FTN_VarBaseTypeMask) &&
          (rangesInfo->nameItemType & FTN_VarBaseTypeMask) ==
          (nameItemType & FTN_VarBaseTypeMask))
        /* Yay we can re-use `rangesInfo' */
        ;
      else                                      /* invalid/no payload */
        rangesInfo = NULL;
    }

  /* If no ...RangesInfo, open one (i.e. parse `rangesFld'), and maybe
   * hang it off `rangesValueWithCooked' so we can re-use it next time:
   */
  if (!rangesInfo)
    {
      rangesInfo = TXsqlFuncLookup_RangesInfo_Open(pmbuf, sqlFuncName, keysFld,
                                                   rangesFld, namesFld, fldop);
      if (!rangesInfo) goto err;
      closeRangesInfo =
        (!rangesValueWithCooked ||
         !TXftiValueWithCooked_SetCookedAndCloseFunc(pmbuf,
                                                     rangesValueWithCooked,
                 rangesInfo, TXsqlFuncLookup_RangesInfo_Close_CookedWrapper));
    }

  /* Parse names, if given: */
  if (namesFld)
    {
      TXftiValueWithCooked                      *namesValueWithCooked = NULL;
      FTN                                       namesType, nameItemType;
      void                                      *cooked;
      TXftiValueWithCooked_CloseCookedFunc      *closeCooked;

      /* Similar prep for names: we need a ...NamesInfo object; look
       * for cooked value first:
       */
      if ((TXfldType(namesFld) & DDTYPEBITS) == FTN_INTERNAL &&
          (fti = getfld(namesFld, NULL)) != NULL &&
          TX_FTI_VALID(fti) &&
          (namesValueWithCooked = tx_fti_getobj(fti)) != NULL)
        {
          /* Prep for verify: */
          TXfldIsMultipleItemType(namesFld, &namesType, &nameItemType);
          /* Verify and use cooked value's payload if possible: */
          if (TXftiValueWithCooked_GetCookedAndCloseFunc(namesValueWithCooked,
                                                    &cooked, &closeCooked) &&
              closeCooked == TXsqlFuncLookup_NamesInfo_Close_CookedWrapper &&
              (namesInfo = (TXsqlFuncLookup_NamesInfo *)cooked) != NULL &&
              /* Types must match: */
              (namesInfo->namesType & FTN_VarBaseTypeMask) ==
              (namesType & FTN_VarBaseTypeMask) &&
              (namesInfo->nameItemType & FTN_VarBaseTypeMask) ==
              (nameItemType & FTN_VarBaseTypeMask))
            /* Yay we can re-use `namesInfo' */
            ;
          else                                  /* invalid/no payload */
            namesInfo = NULL;
        }

      /* If no ...NamesInfo, open one (i.e. parse `namesFld'), and
       * maybe hang it off `namesValueWithCooked' so we can re-use it
       * next time:
       */
      if (!namesInfo)
        {
          namesInfo = TXsqlFuncLookup_NamesInfo_Open(pmbuf, namesFld);
          if (!namesInfo) goto err;
          closeNamesInfo =
            (!namesValueWithCooked ||
             !TXftiValueWithCooked_SetCookedAndCloseFunc(pmbuf,
                                                         namesValueWithCooked,
                   namesInfo, TXsqlFuncLookup_NamesInfo_Close_CookedWrapper));
        }

      /* No useful reason for ranges/names numbers to mismatch; error
       * with message if so.  Note that we check number of *original*
       * bins (from `rangesFld') -- not number of valid bins --
       * against name count: invalid ranges should not affect which
       * name corresponds to future ranges:
       */
      if (rangesInfo->numOrgBins != namesInfo->numNames)
        {
          /* Try to only report once per SQL statement.  If `rangesInfo'/
           * `namesInfo' are cached via TXfldCookedValues, that would
           * handle it, but they may not be cacheable, so use
           * `TXApp->didOncePerSqlMsg':
           */
          if (!TXApp ||
              !TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_LookupRangesNamesNumDiffer])
            {
              if (TXApp)
                TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_LookupRangesNamesNumDiffer] = 1;
              txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
"Number of ranges (%wd) differs from number of names (%wd) given to lookup()",
                             (EPI_HUGEINT)rangesInfo->numOrgBins,
                             (EPI_HUGEINT)namesInfo->numNames);
            }
          goto err;
        }
    }

  /* Reset return fields: */
  if (rangesInfo->returnStrlstBuf)
    {
      htbuf_release(rangesInfo->returnStrlstBuf);
      if (!TXstrlstBufBegin(rangesInfo->returnStrlstBuf)) goto err;
    }
  rangesInfo->returnData = TXfree(rangesInfo->returnData);
  rangesInfo->numReturnUsedBytes = rangesInfo->numReturnAllocedBytes = 0;

  /* Look up each `keysFld' item; add its zero or more bins to return data
   * in `rangesInfo':
   */
  for (keyItemData = NULL, keyItemLen = 0, keyItemIdx = 0;
       (keyItemData = TXfldGetNextItem(keysFld, keyItemData,
                                       keyItemLen, &keyItemLen)) != NULL;
       keyItemIdx++)
    {                                           /* for each `keysFld' item */
      putfldinit(rangesInfo->keyItemFld, keyItemData, keyItemLen);
      if (!TXsqlFuncLookup_FindKeyItemAndAppendBins(pmbuf, rangesInfo,
                                                    namesInfo))
      	goto err;
    }

  /* Get return value: */
  if (rangesInfo->returnStrlstBuf)
    {
      char      *data;

      if (!TXstrlstBufEnd(rangesInfo->returnStrlstBuf)) goto err;
      returnLen = htbuf_getdata(rangesInfo->returnStrlstBuf, &data, 1);
      returnData = data;
    }
  else
    {
      /* Take `rangesInfo->returnData': */
      returnData = rangesInfo->returnData;
      rangesInfo->returnData = NULL;
      returnLen = rangesInfo->numReturnUsedBytes/rangesInfo->returnItemElSz;
      rangesInfo->numReturnUsedBytes = rangesInfo->numReturnAllocedBytes = 0;
      /* Ensure non-NULL data if zero items, so we return zero items
       * not a NULL field:
       */
      if (!returnData) returnData = TXstrdup(pmbuf, __FUNCTION__, "");
    }

  /* Set return value; goes into `keysFld' since that is first arg: */
  if (TXfldmathverb > 0)
    {
      char      *keys;

      keys = fldtostr(keysFld);
      if (keys) keysDup = TXstrdup(pmbuf, __FUNCTION__, keys);
    }
  res = TXsqlSetFunctionReturnData(__FUNCTION__, keysFld, returnData,
                                   rangesInfo->returnType, FTI_UNKNOWN,
                                   rangesInfo->returnItemElSz, returnLen, 0);
  returnData = NULL;                            /* `keysFld' owns it now */
  returnLen = 0;
  if (!res) goto err;

  if (TXfldmathverb > 0)
    txpmbuf_putmsg(pmbuf, MINFO + TRACE_LOOKUP_CALL, __FUNCTION__,
                   "Returning %s `%s' for key(s) %s%s%s",
                   TXfldtypestr(keysFld), fldtostr(keysFld),
                   (keysDup ? "`" : ""), keysDup, (keysDup ? "'" : ""));
  ret = FOP_EOK;                                /* success */
  goto finally;	

err:
  ret = FOP_EINVAL;
finally:
  returnData = TXfree(returnData);
  if (closeRangesInfo)
    rangesInfo = TXsqlFuncLookup_RangesInfo_Close(pmbuf, rangesInfo);
  if (closeNamesInfo)
    namesInfo = TXsqlFuncLookup_NamesInfo_Close(pmbuf, namesInfo);
  fldop = TXreleaseFldopToCache(fldop);
  keysDup = TXfree(keysDup);
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_lookupCanonicalizeRanges(FLD *rangesFld, FLD *keyTypeFld)
/* SQL lookupCanonicalizeRanges(rangesFld, keyTypeFld).
 * SQL return is list of canonical versions of ranges given in `rangesFld'.
 * `keyTypeFld' is string name of key type, for proper bound comparisons.
 * Useful for easier Vortex parsing of lookup() ranges.
 * Returns FOP_EOK on success, else FOP_E... error.
 */
{
  static const char             sqlFuncName[] = "lookupCanonicalizeRanges";
  TXPMBUF                       *pmbuf = NULL;
  char                          *keyTypeData;
  FLD                           *keysFld = NULL;
  FLDOP                         *fldop = NULL;
  int                           ret, res;
  void                          *returnData = NULL;
  size_t                        returnLen = 0;
  TXsqlFuncLookup_RangesInfo    *rangesInfo = NULL;

  if (!(fldop = TXgetFldopFromCache())) goto err;
  if (!keyTypeFld || TXfldbasetype(keyTypeFld) != FTN_CHAR)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                  "Invalid type %s for %s() key type field: Expected varchar",
                     (keyTypeFld ? TXfldtypestr(keyTypeFld) : "NULL"),
                     sqlFuncName);
      goto err;
    }
  keyTypeData = (char *)getfld(keyTypeFld, NULL);
  keysFld = createfld((keyTypeData ? keyTypeData : ""), 1, 1);
  if (!keysFld)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot create key field of type `%s'", keyTypeData);
      goto err;
    }
  rangesInfo = TXsqlFuncLookup_RangesInfo_Open(pmbuf, sqlFuncName, keysFld,
                                               rangesFld, NULL, fldop);
  if (!rangesInfo) goto err;

  returnData = TXsqlFuncLookup_RangesInfo_PrintRanges(pmbuf, rangesInfo,
                                        TXbool_True, TXbool_True, &returnLen);
  if (!returnData) goto err;

  res = TXsqlSetFunctionReturnData(__FUNCTION__, rangesFld, returnData,
                                   FTN_varSTRLST, FTI_UNKNOWN,
                                   TX_STRLST_ELSZ, returnLen, 0);
  returnData = NULL;                            /* `rangesFld' owns it now */
  returnLen = 0;
  if (!res) goto err;
  ret = FOP_EOK;                                /* success */
  goto finally;	

err:
  ret = FOP_EINVAL;
finally:
  returnData = TXfree(returnData);
  rangesInfo = TXsqlFuncLookup_RangesInfo_Close(pmbuf, rangesInfo);
  fldop = TXreleaseFldopToCache(fldop);
  keysFld = closefld(keysFld);
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_lookupParseRange(FLD *rangeFld, FLD *partsFld)
/* SQL lookupParseRange(rangeFld, partsFld).
 * SQL return is list of parts for single given range.
 * For any range, the concatenation of all parts (some may be empty)
 * should equal the original range.
 * Range should be a single return value from lookupCanonicalizeRange().
 * Returns FOP_EOK on success, else FOP_E... error.
 */
{
  static const char             sqlFuncName[] = "lookupParseRange";
  TXPMBUF                       *pmbuf = NULL;
  FLD                           keysFld;
  FLDOP                         *fldop = NULL;
  int                           ret, res;
  char                          *returnData = NULL;
  size_t                        returnLen = 0;
  TXsqlFuncLookup_RangesInfo    *rangesInfo = NULL;
  size_t                        partIdx, partItemLen, partPrevItemLen = 0;
  void                          *partItemData, *partPrevItemData = NULL;
  HTBUF                         *returnBuf = NULL;
  FTN                           partItemType;
  TXsqlFuncLookup_Range         *range;

  if (!(fldop = TXgetFldopFromCache())) goto err;

  initfld(&keysFld, FTN_varCHAR, 1);            /* fake/temp */
  rangesInfo = TXsqlFuncLookup_RangesInfo_Open(pmbuf, sqlFuncName, &keysFld,
                                               rangeFld, NULL, fldop);
  if (!rangesInfo ||
      !(returnBuf = openhtbuf()) ||
      !TXstrlstBufBegin(returnBuf))
    goto err;

  if (rangesInfo->numRanges != 1)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Must give single range");
      goto err;
    }
  range = rangesInfo->ranges[0];

  /* Get each requested part: */
  TXfldIsMultipleItemType(partsFld, NULL, &partItemType);
  for (partIdx = 0;
       (partItemData = TXfldGetNextItem(partsFld, partPrevItemData,
                                      partPrevItemLen, &partItemLen)) != NULL;
       partPrevItemData = partItemData, partPrevItemLen = partItemLen,
         partIdx++)
    {
      TXsqlFuncLookup_Part      partIdx;

      switch (partItemType & DDTYPEBITS)
        {
        case FTN_CHAR:
        case FTN_BYTE:
        case FTN_INDIRECT:
          break;
        default:
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "Unknown range part");
          goto emptyPart;
        }

      /* ID the requested part: */
      partIdx = (TXsqlFuncLookup_Part)-1;
#define CHECK_PART(s)                                   \
      if (partItemLen == strlen(#s) &&                  \
          strnicmp(partItemData, #s, strlen(#s)) == 0)  \
        partIdx = TXsqlFuncLookup_Part_##s
      CHECK_PART(LowerInclusivity);
      CHECK_PART(LowerBound);
      CHECK_PART(RangeOperator);
      CHECK_PART(UpperInclusivity);
      CHECK_PART(UpperBound);
#undef CHECK_PART
      if (partIdx == (TXsqlFuncLookup_Part)-1)
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
                         "Unknown range part `%.*s' ignored", (int)partItemLen,
                         (char *)partItemData);
          goto emptyPart;
        }

      /* Add the part (if present) to return value: */
      if (range->partDataOff[partIdx] != (size_t)(-1))
        {
          if (!TXstrlstBufAddString(returnBuf, (char *)range->data +
                                    range->partDataOff[partIdx],
                                    range->partDataLen[partIdx]))
            goto err;
        }
      else
        {
        emptyPart:
          /* Add an empty string for missing parts, to keep return
           * array in sync with `partsFld' array:
           */
          if (!TXstrlstBufAddString(returnBuf, "", 0))
            goto err;
        }
    }

  if (!TXstrlstBufEnd(returnBuf)) goto err;
  returnLen = htbuf_getdata(returnBuf, &returnData, 1);
  if (!returnData) returnData = TXstrdup(pmbuf, __FUNCTION__, "");
  res = TXsqlSetFunctionReturnData(__FUNCTION__, rangeFld, returnData,
                                   FTN_varSTRLST, FTI_UNKNOWN,
                                   TX_STRLST_ELSZ, returnLen, 0);
  returnData = NULL;                            /* `rangeFld' owns it now */
  returnLen = 0;
  if (!res) goto err;
  ret = FOP_EOK;                                /* success */
  goto finally;	

err:
  ret = FOP_EINVAL;
finally:
  returnData = TXfree(returnData);
  rangesInfo = TXsqlFuncLookup_RangesInfo_Close(pmbuf, rangesInfo);
  fldop = TXreleaseFldopToCache(fldop);
  returnBuf = closehtbuf(returnBuf);
  return(ret);
}
