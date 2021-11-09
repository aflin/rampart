#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include "texint.h"

/* ALTER INDEX tokens: */
typedef enum
{
  TXAITOK_UNKNOWN = -1,                         /* must be first */
  TXAITOK_OPTIMIZE,
  TXAITOK_REBUILD,
  TXAITOK_DROPFIRST,                            /* option */
  TXAITOK_NUM                                   /* must be last */
}
TXAITOK;

static CONST char * CONST       TXalterIndexTokenStrs[TXAITOK_NUM] =
{
  "OPTIMIZE",
  "REBUILD",
  "DROPFIRST",
};

static CONST char       TXcommaWhitespace[] = ", \t\r\n\v\f";

/* ------------------------------------------------------------------------ */

static TXAITOK TXstrToAlterIndexToken ARGS((CONST char *s, CONST char *e));
static TXAITOK
TXstrToAlterIndexToken(s, e)
CONST char      *s;     /* (in) string token */
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
{
  TXAITOK       tok;

  if (!e) e = s + strlen(s);
  for (tok = (TXAITOK)0; tok < TXAITOK_NUM; tok++)
    if (strnicmp(s, TXalterIndexTokenStrs[tok], e - s) == 0 &&
        TXalterIndexTokenStrs[tok][e - s] == '\0')
      return(tok);
  return(TXAITOK_UNKNOWN);
}

static CONST char *TXalterIndexTokenToStr ARGS((TXAITOK tok));
static CONST char *
TXalterIndexTokenToStr(tok)
TXAITOK tok;
{
  if ((unsigned)tok < (unsigned)TXAITOK_NUM)
    return(TXalterIndexTokenStrs[(unsigned)tok]);
  return("unknown");
}

/* Does actual work of altering index `indexName'.
 * Returns 2 if ok, 1 if not found live (silent), 0 on error.
 */
static int
TXalterIndex(DDIC *ddic, CONST char *indexName, TXAITOK action, TXAITOK option, PRED *conditions)
{
  static CONST char     fn[] = "TXalterIndex";
  int                   numIndexes = 0, ret, i, updindexFlags;
  char                  *indexTypes = NULL, **indexFiles = NULL;
  char                  **indexTables = NULL,  **indexFields = NULL;
  char                  **sysindexParams = NULL;
  TXindOpts             *options = NULL;

  numIndexes = ddgetindexbyname(ddic, (char *)indexName, &indexTypes, NULL,
                                &indexFiles, &indexTables, &indexFields,
                                &sysindexParams);
  if (numIndexes < 0)                           /* error */
    {
      numIndexes = 0;                           /* for TXfreeStrList() */
      goto err;
    }
  if (numIndexes == 0)
    {
      /* Index not found; caller determines if this is an error or not: */
      ret = 1;                                  /* not found */
      goto done;
    }

  updindexFlags = 0;
  switch (option)
    {
    case TXAITOK_DROPFIRST:
      if (action != TXAITOK_REBUILD)
        {
          putmsg(MERR + UGE, fn,
        "DROPFIRST option is only valid with REBUILD action for ALTER INDEX");
          goto err;
        }
      /* WTF implement DROPFIRST in updindex(): */
      putmsg(MERR + UGE, fn, "DROPFIRST option not currently supported");
      goto err;
      updindexFlags |= 0x4;                     /* drop first */
      break;
    case TXAITOK_UNKNOWN:                       /* none given */
      break;
    default:
      putmsg(MERR + UGE, fn, "Unknown ALTER INDEX option `%s'",
             TXalterIndexTokenToStr(option));
      goto err;
    }

  /* Perform action on the index.  Should only be one live index named
   * `indexName', but it might exist multiple times if deleted/updating:
   */
  switch (action)
    {
    case TXAITOK_OPTIMIZE:
      break;
    case TXAITOK_REBUILD:
      updindexFlags |= 0x2;               /* rebuild */
      break;
    default:
      goto unknownAction;
    }
  for (i = 0; i < numIndexes; i++)
    switch (indexTypes[i])
      {
      case INDEX_3DB:
      case INDEX_FULL:
      case INDEX_MM:
        if (!(options = TXindOptsOpen(ddic))) goto err;
        ret = (updindex(ddic, (char *)indexName, updindexFlags, options, conditions) == 0 ? 2 : 0);
        goto done;
      case INDEX_BTREE:
      case INDEX_INV:
        switch (action)
          {
          case TXAITOK_OPTIMIZE:
            /* OPTIMIZE is a no-op for non-Metamorph indexes: */
            ret = 2;
            break;
          case TXAITOK_REBUILD:
            if (!(options = TXindOptsOpen(ddic))) goto err;
            ret = (updindex(ddic, (char *)indexName, updindexFlags, options, conditions) == 0 ? 2 : 0);
            break;
          default:
            goto unknownAction;
          }
        goto done;
      case INDEX_3CR:
      case INDEX_MMCR:
      case INDEX_FULLCR:
      case INDEX_CR:
        switch (action)
          {
          case TXAITOK_OPTIMIZE:
            /* OPTIMIZE is a no-op if index is being created: */
            ret = 2;
            break;
          case TXAITOK_REBUILD:
            putmsg(MERR + UGE, fn,
                   "Cannot rebuild index %s: Currently being created",
                   indexName);
            goto err;
          default:
          unknownAction:
            putmsg(MERR + UGE, fn, "Unknown ALTER INDEX action `%s'",
                   TXalterIndexTokenToStr(action));
            goto err;
          }
        goto done;
      case INDEX_DEL:
      case INDEX_TEMP:
        /* Silently ignore as if not found: */
        break;
      default:
        putmsg(MERR, fn,
               "Index %s is unknown type %c", indexName, indexTypes[i]);
        goto err;
      }
  /* Live index not found: */
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  options = TXindOptsClose(options);
  indexTypes = TXfree(indexTypes);
  indexFiles = TXfreeStrList(indexFiles, numIndexes);
  indexTables = TXfreeStrList(indexTables, numIndexes);
  indexFields = TXfreeStrList(indexFields, numIndexes);
  sysindexParams = TXfreeStrList(sysindexParams, numIndexes);
  return(ret);
}

/**
 * Does ALTER INDEX action: OPTIMIZE | {REBUILD [DROPFIRST]} for all
 * indexes matched by `indexName' and/or `tableName'.
 *
 * Returns 0 on error.
 *
 * @param ddic          data dictionary
 * @param indexName     index name (NULL: all)
 * @param tableNames    table name (NULL: all)
 * @param actionOptions space-separated action and options
 * @param conditions    predicate with condition when to perform action
 */
int
TXalterIndexes(DDIC *ddic, CONST char *indexName, CONST char *tableName, CONST char *actionOptions, PRED *conditions)
{
  static CONST char     fn[] = "TXalterIndexes";
  int                   ret, numIndexes = 0, i, foundLive, foundIndex = 0;
  char                  **indexNames = NULL, *tmp;
  TXAITOK               action, option;
  CONST char            *s, *e;

#ifndef TX_ALTER_INDEX
  if (!getenv("TX_ALTER_INDEX"))
    {
      putmsg(MERR + UGE, CHARPN, "ALTER INDEX not supported yet");
      goto err;
    }
#endif /* !TX_ALTER_INDEX */

  /* Parse `actionOptions': */
  s = actionOptions;
  s += strspn(s, TXcommaWhitespace);
  e = s + strcspn(s, TXcommaWhitespace);
  action = TXstrToAlterIndexToken(s, e);
  switch (action)
    {
    case TXAITOK_OPTIMIZE:
    case TXAITOK_REBUILD:
      break;
    default:
      putmsg(MERR + UGE, fn, "Unknown ALTER INDEX action `%.*s'",
             (int)(e - s), s);
      goto err;
    }
  s = e;                                        /* after action */
  s += strspn(s, TXcommaWhitespace);
  e = s + strcspn(s, TXcommaWhitespace);
  if (e > s)                                    /* an option is present */
    {
      option = TXstrToAlterIndexToken(s, e);
      switch (option)
        {
        case TXAITOK_DROPFIRST:
          break;
        default:
          putmsg(MERR + UGE, fn, "Unknown ALTER INDEX option `%.*s'",
                 (int)(e - s), s);
          goto err;
        }
    }
  else
    option = TXAITOK_UNKNOWN;                   /* i.e. none */
  s = e;                                        /* after option */
  s += strspn(s, TXcommaWhitespace);
  e = s + strcspn(s, TXcommaWhitespace);
  if (e > s)                                    /* more opions (unhandled) */
    {
      putmsg(MERR + UGE, fn, "Unknown/excess ALTER INDEX option `%.*s'",
             (int)(e - s), s);
      goto err;
    }

  /* It may take a while to alter index(es), thus we should not lock
   * SYSINDEX for the duration, thus index info (and existence) may
   * change during this call.  Just snapshot index names to alter;
   * re-get index type/file/etc. later as we are about to process each.
   * Do a cleanup of deleted/temp indexes first, if any:
   */
  TXdocleanup(ddic);
  numIndexes = TXddgetindexinfo(ddic, (char *)tableName, NULL, NULL, NULL,
                                &indexNames, NULL, NULL, NULL, NULL);
  if (numIndexes < 0) goto err;                 /* error getting index info */
  if (numIndexes == 0)                          /* no index(es) found */
    {
      if (indexName) goto indexNotFound;
      /* Not looking for a specific index, e.g. `ALTER INDEX ALL [ON table]'.
       * Thus it is ok not to find any index(es); return ok silently:
       */
      goto ok;
    }

  /* If specific index requested, trim the list to just that name: */
  if (indexName)
    {
      for (i = 0; i < numIndexes && strcmp(indexNames[i], indexName)!=0; i++);
      if (i >= numIndexes) goto indexNotFound;  /* index not found */
      /* Move found index to start of list: */
      tmp = indexNames[0];
      indexNames[0] = indexNames[i];
      indexNames[i] = tmp;
      /* Free the rest of the list: */
      for (i = 1; i < numIndexes; i++)
        indexNames[i] = TXfree(indexNames[i]);
      numIndexes = 1;
      foundIndex = 1;
    }

  /* Alter each index per `action'/`option': */
  foundLive = 0;
  for (i = 0; i < numIndexes; i++)
    switch (TXalterIndex(ddic, indexNames[i], action, option, conditions))
      {
      case 2:                                   /* success */
        foundLive = 1;
        break;
      case 1:                                   /* not found live */
        /* Not finding a live version of an index is only an error if
         * we are looking for a specific named index; this is checked
         * below.  Otherwise (all indexes) it was probably dropped
         * after we got `indexNames'.
         */
        break;
      case 0:                                   /* error */
        foundLive = 1;
      default:
        goto err;
      }
  if (!foundLive && indexName)
    {
    indexNotFound:
      putmsg(MERR + UGE, fn, "Index %s%s%s %s in database %s",
             indexName, (tableName ? " on table " : ""),
             (tableName ? tableName : ""),
             (foundIndex ? "has been deleted" : "does not exist"),
             ddic->epname);
      goto err;
    }
ok:
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  indexNames = TXfreeStrList(indexNames, numIndexes);
  return(ret);
}
