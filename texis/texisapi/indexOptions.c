#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "texint.h"


static CONST char * CONST EmptyList[] = { "" };
static CONST char * CONST TXindexOptionNames[TXindOpt_NUM] =
{
#undef I
#define I(tok)  #tok,
  TX_INDEX_OPTIONS_SYMBOLS_LIST
#undef I
};

/* ------------------------------------------------------------------------ */

static char *
TXindOptsGetNextItemStr(QNODE *qnode, void **itemPtr, size_t *itemLen)
/* Returns next item of value of leaf `qnode', as a string, or NULL if
 * no more items.  `*itemPtr'/`*itemLen' maintain state (start at NULL/0).
 * NOTE: neither `qnode' nor its FLD should be modified between calls.
 * NOTE: return value should be copied immediately; possible static data.
 */
{
  PARAM         *param;
  FLD           *fld;

  if (!qnode) goto eof;
  switch (qnode->op)
    {
    case PARAM_OP:
      param = qnode->tname;
      if (!param) goto eof;
      fld = param->fld;
      goto getFldItem;
    case FIELD_OP:
      fld = qnode->tname;
    getFldItem:
      if (!fld) goto eof;
      return(TXfldGetNextItemStr(fld, itemPtr, itemLen));
    case NAME_OP:
      if (*itemPtr) goto eof;                   /* single item already seen */
      *itemPtr = qnode->tname;
      *itemLen = strlen(qnode->tname);
      return(qnode->tname);
    default:
    eof:
      *itemPtr = NULL;
      *itemLen = 0;
      return(NULL);
    }
}

/* ------------------------------------------------------------------------ */

static size_t
TXindOptsWalkValues(QNODE *qnode, char ***values, size_t nextIdx,
                    size_t *allocedNum)
/* Recursively walks `qnode' LIST_OP tree, adding to `*values' list
 * (re-allocing if needed).  For obtaining a single index option's
 * multiple values.
 * Returns next value's array index (i.e. # of values obtained); -1 on error.
 * Caller must NULL-terminate array if desired.
 */
{
  static CONST char     fn[] = "TXindOptsWalkValues";
  char                  *s;
  void                  *itemPtr;
  size_t                itemLen;

  if (!qnode || nextIdx == (size_t)(-1)) goto err;
  switch (qnode->op)
    {
    case LIST_OP:
      nextIdx = TXindOptsWalkValues(qnode->left, values, nextIdx, allocedNum);
      if (nextIdx == (size_t)(-1)) goto err;
      nextIdx = TXindOptsWalkValues(qnode->right, values, nextIdx,allocedNum);
      break;
    default:                                    /* e.g. a FIELD_OP value */
      itemPtr = NULL;
      itemLen = 0;
      while ((s = TXindOptsGetNextItemStr(qnode, &itemPtr, &itemLen)) != NULL)
        {
          if (!TX_INC_ARRAY(TXPMBUFPN, values, nextIdx, allocedNum))
            goto err;
          (*values)[nextIdx] = s = TXstrdup(TXPMBUFPN, fn, s);
          if (!s) goto err;
          nextIdx++;                            /* we added a value */
        }
      break;
    }
  goto done;

err:
  /* Original caller should free arrays, so we do not free them
   * out from under an intermediate recursive call:
   */
  nextIdx = (size_t)(-1);
done:
  return(nextIdx);
}

/* ------------------------------------------------------------------------ */

int
TXindOptsGetRawOptions(options, qnode)
TXindOpts       *options;       /* (in/out) options to write to */
QNODE           *qnode;         /* (in) tree to get options from */
/* Recursively walks `qnode' LIST_OP tree, adding index options and
 * values to `options'.
 * Note: options must be processed afterwards with
 * TXindOptsProcessRawOptions().
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXindOptsGetRawOptions";
  char                  *s, **curVal = NULL;
  void                  *itemPtr;
  size_t                curValLen = 0, curValAllocedLen = 0, itemLen;
  int                   ret, o;
  TXindOpt              opt;

#ifndef TX_INDEX_OPTIONS
  static int    ok = -1;
  if (ok == -1) ok = (getenv("TX_INDEX_OPTIONS") != NULL);
  if (!ok)
    {
      putmsg(MERR + UGE, NULL,
             "CREATE INDEX ... WITH options not supported yet");
      goto err;
    }
#endif /* !TX_INDEX_OPTIONS */

  if (!qnode) goto err;
  switch (qnode->op)
    {
    case LIST_OP:
      if (!TXindOptsGetRawOptions(options, qnode->left) ||
          !TXindOptsGetRawOptions(options, qnode->right))
        goto err;
      break;
    case COLUMN_OP:                             /* option with value(s) */
      if (!qnode->left || !qnode->right) goto err;
      itemPtr = NULL;
      itemLen = 0;
      s = TXindOptsGetNextItemStr(qnode->left, &itemPtr, &itemLen);
      if (!s) goto err;
      opt = TXindOptStringToEnum(s);
      if (opt == TXindOpt_Unknown) goto unknownOption;
      for (o = 0; o < options->numOptions; o++)
        if (options->option[o] == opt) goto duplicateOption;
      options->option[options->numOptions] = opt;
      if (qnode->right->op == LIST_OP)          /* multi-value option */
        {
          curValLen = TXindOptsWalkValues(qnode->right, &curVal, 0,
                                          &curValAllocedLen);
          if (curValLen == (size_t)(-1)) goto err;
        }
      else                                      /* single-value option */
        {
          /* Might be a multi-value strlst: */
          itemPtr = NULL;
          itemLen = 0;
          curValLen = 0;
          while ((s = TXindOptsGetNextItemStr(qnode->right, &itemPtr,
                                              &itemLen)) != NULL)
            {
              if (!TX_INC_ARRAY(TXPMBUFPN, &curVal, curValLen,
                                &curValAllocedLen))
                goto err;
              curVal[curValLen++] = s = TXstrdup(TXPMBUFPN, fn, s);
              if (!s) goto err;
            }
        }
      if (!TX_INC_ARRAY(TXPMBUFPN, &curVal, curValLen, &curValAllocedLen))
        goto err;
      curVal[curValLen] = NULL;
      options->values[options->numOptions++] = curVal;
      curVal = NULL;
      curValLen = curValAllocedLen = 0;
      break;
    default:                                    /* option without a value */
      itemPtr = NULL;
      itemLen = 0;
      s = TXindOptsGetNextItemStr(qnode, &itemPtr, &itemLen);
      if (!s) goto err;
      opt = TXindOptStringToEnum(s);
      if (opt == TXindOpt_Unknown)
        {
        unknownOption:
          putmsg(MERR + UGE, fn, "Unknown index option `%s'", s);
          goto err;
        }
      for (o = 0; o < options->numOptions; o++)
        if (options->option[o] == opt)
          {
          duplicateOption:
            putmsg(MERR + UGE, fn,
   "Index option `%s' specified multiple times: must be present at most once",
                   TXindOptEnumToString(opt));
            goto err;
          }
      options->option[options->numOptions] = opt;
      options->values[options->numOptions] =    /* in case alloced */
        TXfreeStrList(options->values[options->numOptions], -1);
      options->numOptions++;
      break;
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  if (curValLen == (size_t)(-1)) curValLen = 0; /* for TXfreeStrList() */
  if (curVal) curVal = TXfreeStrList(curVal, curValLen);
  return(ret);
}

/* ------------------------------------------------------------------------ */

TXindOpt
TXindOptStringToEnum(s)
const char      *s;
/* Returns TXindOpt value for `s', or TXindOpt_Unknown if unknown.
 */
{
  TXindOpt      l, r, i;
  int           cmp;

  l = (TXindOpt)0;
  r = TXindOpt_NUM;
  while (l < r)                                 /* binary search */
    {
      i = (l + r)/2;
      cmp = strcmpi(s, TXindexOptionNames[i]);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else return(i);
    }
  return(TXindOpt_Unknown);
}

/* ------------------------------------------------------------------------ */

const char *
TXindOptEnumToString(opt)
TXindOpt        opt;
/* Returns string name for `opt', or "unknown" if unknown.
 */
{
  if ((unsigned)opt >= (unsigned)TXindOpt_NUM) return("unknown");
  return(TXindexOptionNames[opt]);
}

/* ------------------------------------------------------------------------ */

TXindOpts *
TXindOptsClose(options)
TXindOpts       *options;
{
  int   i;

  if (!options) return(NULL);
  for (i = 0; i < options->numOptions; i++)
    if (options->values[i])
      options->values[i] = TXfreeStrList(options->values[i], -1);
  options->indexspace = TXfree(options->indexspace);
  options->noiselist = TXfreeStrEmptyTermList(options->noiselist, -1);
  options->wordExpressions =
    TXfreeStrEmptyTermList(options->wordExpressions, -1);
  options = TXfree(options);
  return(NULL);
}

/* ------------------------------------------------------------------------ */

TXindOpts *
TXindOptsOpen(ddic)
DDIC    *ddic;  /* (in) DDIC for some defaults */
/* Creates a TXindOpts structure, initialized to global/`ddic' defaults.
 */
{
  static CONST char     fn[] = "TXindOptsOpen";
  TXindOpts             *options = NULL;

  if (!globalcp && !(globalcp = TXopenapicp())) goto err;

  options = (TXindOpts *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(TXindOpts));
  if (!options) goto err;

  /* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
  BTPARAM_INIT(&options->btparams);
  BTPARAM_INIT_TO_PROCESS_DEFAULTS(&options->btparams, ddic);
  options->textsearchmode = globalcp->textsearchmode;
  options->indexspace = TXstrdup(TXPMBUFPN, fn, ddic->indspc);
  if (!options->indexspace) goto err;
  options->keepnoise = globalcp->keepnoise;
  options->noiselist = TXdupStrEmptyTermList(TXPMBUFPN, fn,
                                             (char **)globalcp->noise, -1);
  if (!options->noiselist) goto err;
  options->wordExpressions = TXdupStrEmptyTermList(TXPMBUFPN, fn,
                                                   TXgetglobalexp(), -1);
  if (!options->wordExpressions) goto err;
  options->fdbiVersion = TxFdbiVersion;
  options->fdbiMaxSingleLocs = TxFdbiMaxSingleLocs;
  options->indexmem = TXcomputeIndexmemValue(TXindexmemUser);
  options->indexmeter = TXindexmeter;
  goto done;

err:
  options = TXindOptsClose(options);
done:
  return(options);
}

/* ------------------------------------------------------------------------ */

static int
TXindOptIsSingleValue(TXindOpts *options, TXindOpt opt)
/* Returns nonzero if option `opt' is single value, 0 if not (and yaps).
 */
{
  char  **vals;
  int   i;

  for (i = 0; i < options->numOptions && options->option[i] != opt; i++);
  vals = (i < options->numOptions ? options->values[i] : NULL);
  if (!vals || !vals[0] || vals[1])
    {
      putmsg(MERR + UGE, CHARPN, "Index option `%s' must be a single value",
             TXindOptEnumToString(opt));
      return(0);
    }
  return(1);
}

/* ------------------------------------------------------------------------ */

int
TXindOptsProcessRawOptions(options, indexType, forUpdate)
TXindOpts       *options;       /* (in/out) `WITH ...' options */
int             *indexType;     /* (in/out) index type */
int             forUpdate;      /* (in) nonzero: index update not create */
/* Processes raw `options->options[]', setting `options' cooked values,
 * and yaps and returns 0 if invalid or non-applicable etc. options in
 * `options'.  Updates `*indexType'.  Called after (optional)
 * TXindOptsGetRawOptions().
 * NOTE: see also setprop(), which parses the global version of some options.
 */
{
  static CONST char     fn[] = "TXindOptsProcessRawOptions";
  static CONST char     whitespace[] = " \t\r\n\v\f";
  int                   ret, res, intVal, optIdx;
  TXindOpt              opt;
  EPI_HUGEINT           h;
  char                  *s, *e, **noiselist, **wordExpressions;
  TXCFF                 mode;
  TXindexValues         indexValues;
  TXMDT                 meterType;
#define IS_METAMORPH_TYPE(i)    \
  ((i) == INDEX_MM || (i) == INDEX_FULL || (i) == INDEX_3DB2)

  if (options->wasProcessed)
    {
      /* Already processed; can return silently (reprocessing might be
       * erroneous, because cooked values are no longer process defaults).
       * But if this processing has a different `forUpdate' value,
       * it may be a problem, as TXindOpt_counts etc. behave differently:
       */
      if (options->wasProcessed != (forUpdate ? 2 : 1))
        {
          putmsg(MERR, fn,
                 "Internal error: attempt to double-process index options for different create/update state");
          goto err;
        }
      return(2);
    }

  /* Parse options in given order, so later options can replace earlier
   * (e.g. counts/expressions, indexversion/indexmaxsingle):
   */
  for (optIdx = 0; optIdx < options->numOptions; optIdx++)
    {
      opt = options->option[optIdx];
      switch (opt)
        {
          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_counts:
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          res = TXgetBooleanOrInt(TXPMBUFPN, "index option",
                                  TXindOptEnumToString(opt),
                                  options->values[optIdx][0], NULL, 4);
          if (res < 0) goto err;
          if (res)                              /* COUNTS ON */
            switch (*indexType)
              {
              case INDEX_3DB2:                  /* metamorph counter */
                /* counts already on; no change */
                break;
              case INDEX_3DB:
              case INDEX_MM:
                /* COUNTS is currently 'off': cannot be set 'on' at
                 * index update, only at create.  But at index update,
                 * we silently ignore such options that cannot be
                 * changed at update, e.g. counts, wordpositions etc.
                 * We could complain and/or fail, but that makes
                 * updating an index via re-issuing the
                 * create-w/options statement fail, and it is
                 * inconsistent with ignoring the Metamorph variant
                 * type during index update as we have done
                 * pre-version-7.
                 */
                *indexType = INDEX_3DB2;
                break;
              case INDEX_FULL:
                /* wtf there is no metamorph inverted counter index type yet */
                goto optionCannotBeOnOffForIndexType;
              default:
                goto optionCannotBeSetForIndexType;
              }
          else                                  /* COUNTS OFF */
            switch (*indexType)
              {
              case INDEX_3DB2:                  /* metamorph counter */
              case INDEX_3DB:
                *indexType = INDEX_MM;
                break;
              case INDEX_MM:
              case INDEX_FULL:
                break;
              default:
                goto optionCannotBeSetForIndexType;
              }
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_wordpositions:
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          res = TXgetBooleanOrInt(TXPMBUFPN, "index option",
                                  TXindOptEnumToString(opt),
                                  options->values[optIdx][0], NULL, 4);
          if (res < 0) goto err;
          if (res)                              /* WORDPOSITIONS ON */
            switch (*indexType)
              {
              case INDEX_FULL:
                /* wordpositions already on */
                break;
              case INDEX_3DB:
              case INDEX_MM:
                *indexType = INDEX_FULL;
                break;
              case INDEX_3DB2:                  /* metamorph counter */
                /* wtf there is no metamorph inverted counter index type */
                goto optionCannotBeOnOffForIndexType;
              default:
                goto optionCannotBeSetForIndexType;
              }
          else                                  /* WORDPOSITIONS OFF */
            switch (*indexType)
              {
              case INDEX_3DB2:                  /* metamorph counter */
                break;
              case INDEX_FULL:
              case INDEX_3DB:
              case INDEX_MM:
                *indexType = INDEX_MM;
                break;
              default:
              optionCannotBeSetForIndexType:
                res = -1;                       /* cannot be set at all */
              optionCannotBeOnOffForIndexType:
                putmsg(MERR + UGE, fn,
                       "Index option `%s' cannot be %s for %s index type",
                       TXindOptEnumToString(opt), (res < 0 ? "set" :
                                    (res > 0 ? "turned on" : "turned off")),
                       TXgetIndexTypeDescription(*indexType));
                goto err;
              }
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
          /* NOTE: see also TXtextParamsToBtparam(), which parses the
           * SYSINDEX.PARAMS version of these; also see setprop():
           */
        case TXindOpt_max_index_text:
          if (IS_METAMORPH_TYPE(*indexType))
            {
            optionCannotBeSetForMetamorphTypes:
              putmsg(MERR + UGE, fn,
                  "Index option `%s' cannot be set for Metamorph index types",
                     TXindOptEnumToString(opt));
              goto err;
            }
          /* max_index_text and some other options cannot be changed
           * at index update, but non-metamorph types cannot be
           * updated at all (checked later), so we let that later
           * check fail the statement instead of failing here.
           */
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          intVal = (int)strtol(s = options->values[optIdx][0], &e, 0);
          if (e == s || e[strspn(e, whitespace)])
            goto invalidValue;
          options->btparams.max_index_text = intVal;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_stringcomparemode:
          if (IS_METAMORPH_TYPE(*indexType))
            goto optionCannotBeSetForMetamorphTypes;
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          /* `WITH ...' options stringcomparemode can be relative,
           * because we can get the current/default setting:
           */
          if (!globalcp && !(globalcp = TXopenapicp())) goto err;
          if (!TXstrToTxcff(options->values[optIdx][0], CHARPN,
                            globalcp->textsearchmode,
                            globalcp->stringcomparemode,
                            options->btparams.stringcomparemode, 1,
                            (TXCFF)(-1), &mode))
            goto invalidValue;
          options->btparams.stringcomparemode = mode;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_indexvalues:
          if (IS_METAMORPH_TYPE(*indexType))
            goto optionCannotBeSetForMetamorphTypes;
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          indexValues = TXstrToIndexValues(options->values[optIdx][0],CHARPN);
          if (indexValues == (TXindexValues)(-1)) goto invalidValue;
          options->btparams.indexValues = indexValues;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_textsearchmode:
          if (!IS_METAMORPH_TYPE(*indexType))
            {
            optionCanOnlyBeSetForMetamorphTypes:
              putmsg(MERR + UGE, fn,
                "Index option `%s' can only be set for Metamorph index types",
                     TXindOptEnumToString(opt));
              goto err;
            }
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          /* `WITH ...' options textsearchmode can be relative,
           * because we can get the current/default setting:
           */
          if (!globalcp && !(globalcp = TXopenapicp())) goto err;
          if (!TXstrToTxcff(options->values[optIdx][0], CHARPN,
                            globalcp->textsearchmode,
                            globalcp->stringcomparemode,
                            options->textsearchmode, 0,
                            (TXCFF)(-1), &mode))
            goto invalidValue;
          options->textsearchmode = mode;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_indexspace:
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          options->indexspace = TXfree(options->indexspace);
          options->indexspace = TXdupwsep(options->values[optIdx][0]);
          if (!options->indexspace) goto err;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_keepnoise:
          if (!IS_METAMORPH_TYPE(*indexType))
            goto optionCanOnlyBeSetForMetamorphTypes;
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          res = TXgetBooleanOrInt(TXPMBUFPN, "index option",
                                  TXindOptEnumToString(opt),
                                  options->values[optIdx][0], NULL, 4);
          if (res < 0) goto err;
          options->keepnoise = res;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_noiselist:
          if (!IS_METAMORPH_TYPE(*indexType))
            goto optionCanOnlyBeSetForMetamorphTypes;
          noiselist = (options->values[optIdx] ?
                       (char **)options->values[optIdx] : (char **)EmptyList);
          options->noiselist = TXfreeStrEmptyTermList(options->noiselist, -1);
          options->noiselist =
            TXdupStrEmptyTermList(TXPMBUFPN, fn, noiselist, -1);
          if (!options->noiselist) goto err;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_wordexpressions:
          if (!IS_METAMORPH_TYPE(*indexType))
            goto optionCanOnlyBeSetForMetamorphTypes;
          /* Metamorph counter indexes use a fixed word expressions list: */
          if (*indexType == INDEX_3DB2) goto optionCannotBeSetForIndexType;
          wordExpressions = (options->values[optIdx] ?
                             (char **)options->values[optIdx] :
                             (char **)EmptyList);
          options->wordExpressions =
            TXfreeStrEmptyTermList(options->wordExpressions, -1);
          options->wordExpressions = TXdupStrEmptyTermList(TXPMBUFPN, fn,
                                                         wordExpressions, -1);
          if (!options->wordExpressions) goto err;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_indexversion:
          if (!IS_METAMORPH_TYPE(*indexType))
            goto optionCanOnlyBeSetForMetamorphTypes;
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          intVal = (int)strtol(s = options->values[optIdx][0], &e, 0);
          if (e == s || e[strspn(e, whitespace)])
            goto invalidValue;
          if (!TXfdbiApplyVersion(intVal, &options->fdbiVersion,
                                  &options->fdbiMaxSingleLocs))
            goto err;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_indexmaxsingle:
          if (!IS_METAMORPH_TYPE(*indexType))
            goto optionCanOnlyBeSetForMetamorphTypes;
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          intVal = (int)strtol(s = options->values[optIdx][0], &e, 0);
          if (e == s || e[strspn(e, whitespace)])
            goto invalidValue;
          if (!TXfdbiApplyMaxSingleLocs(intVal, options->fdbiVersion,
                                        &options->fdbiMaxSingleLocs))
            goto err;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_indexmem:
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          if (!tx_parsesz(TXPMBUFPN, options->values[optIdx][0], &h,
                          TXindOptEnumToString(opt), EPI_OS_SIZE_T_BITS,
                          TXbool_True))
            goto err;
          if (h < (EPI_HUGEINT)0) h = (EPI_HUGEINT)0;
          options->indexmem = TXcomputeIndexmemValue((size_t)h);
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        case TXindOpt_indexmeter:
          if (!TXindOptIsSingleValue(options, opt)) goto err;
          meterType = meter_str2type(options->values[optIdx][0], CHARPN);
          if (meterType == TXMDT_INVALID) goto invalidValue;
          options->indexmeter = meterType;
          break;

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
        default:
          putmsg(MERR, fn, "Internal error: Unknown option #%d", (int)opt);
          goto err;
        }
    }

  ret = 1;                                      /* success */
  goto done;

invalidValue:
  putmsg(MERR + UGE, fn, "Invalid `%s' value `%s'",
         TXindOptEnumToString(opt), options->values[optIdx][0]);
err:
  ret = 0;
done:
  options->wasProcessed = (forUpdate ? 2 : 1);
  return(ret);
}
