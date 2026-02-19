/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(MSDOS) && !defined(__MINGW32__)
#include <unistd.h>
#include <pwd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

/******************************************************************/

static int
indexscore(char *indexFields,           /* (in) fields from index */
           int indexType,               /* (in) INDEX_... type */
           const char *indexPath,       /* (in) index path */
           const char *sysindexParams,  /* (in) SYSINDEX.PARAMS of index */
           DBTBL *dbtbl,                /* (in) DBTBL table index is on */
           SLIST *desiredFields,        /* (in) fields from TBSPEC we want */
           QNODE_OP fldOp,              /* (in) FOP_... operation */
           FLD *param,                  /* (in) parameter */
           TXbool paramIsRHS)           /* (in) true: param is right-side */
/* Returns score of given index (greater is better), or 0 if index cannot
 * be used.
 */
{
	int rc = 0, subScore, ret;
	char *curIndexField = NULL;
	char *indexFieldsDup = NULL;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	/* First, get major score based on fields that match: */
	if(!indexFields)
		goto err;
	indexFieldsDup = TXstrdup(pmbuf, __FUNCTION__, indexFields);
	if(!indexFieldsDup)
		goto err;
	curIndexField = strtok(indexFieldsDup, ", ");
	while (curIndexField)
	{
		if(slfind(desiredFields, curIndexField))
			rc++;
		curIndexField = strtok(NULL, ", ");
	}
	indexFieldsDup = TXfree(indexFieldsDup);

	/* Next, get sub-index score based on index settings: */
	switch (indexType)
	{
	case INDEX_BTREE:
		subScore = TXbtreeScoreIndex(indexFields, sysindexParams,
					     dbtbl->ddic->options, indexPath,
					     dbtbl, fldOp, param, paramIsRHS);
		break;
	case INDEX_MM:
	case INDEX_FULL:
		subScore = TX3dbiScoreIndex(indexType, sysindexParams,
					    dbtbl->ddic->options, indexPath,
                                            fldOp);
		break;
	default:
		subScore = 1;
		break;
	}
	if (subScore == 0) goto err;

	/* Merge scores together such that field score has precedence: */
	ret = rc*(TX_INDEX_SUBSCORE_MAX + 1) + subScore;
	goto done;

err:
	ret = 0;
done:
	return(ret);
}

/******************************************************************/

static int iscorecmp ARGS((CONST void *, CONST void *));

static int
iscorecmp(a, b)
CONST void *a;
CONST void *b;
/* qsort() sort callback for INDEXSCORE items.  Higher score sorts first.
 */
{
	CONST INDEXSCORE *isa, *isb;
	int		cmp;

	isa = a; isb = b;
	cmp = isb->score - isa->score;
	if (cmp != 0) return(cmp);
	/* Sort by index path finally, for consistent Vortex test640
	 * etc. output; sort by `orgArrayIdx' is not enough:
	 */
	if (!isa->indexinfo || !isb->indexinfo) return(0);
	return(TXpathcmp(isa->indexinfo->paths[isa->orgArrayIdx], -1,
			 isb->indexinfo->paths[isa->orgArrayIdx], -1));
}

/******************************************************************/

int
TXchooseindex(indexinfo, dbtbl, fop, param, paramIsRHS)
INDEXINFO *indexinfo;	/* (in/out) */
DBTBL	*dbtbl;		/* (in) DBTBL for options */
QNODE_OP fop;		/* (in) operation to choose for */
FLD	*param;		/* (in) parameter */
int	paramIsRHS;	/* (in) nonzero: `param' is right-hand-side of op */
{
	int	j, thistype;
	int start;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	if(!indexinfo)
		return -1;

	if(!indexinfo->initialized)
	{
		if(!indexinfo->iscores)
		{
			indexinfo->iscores = (INDEXSCORE *)TXcalloc
				(pmbuf, __FUNCTION__, indexinfo->numIndexes,
				 sizeof(INDEXSCORE));
			if(!indexinfo->iscores)
				return -1;
			for(j=0; j < indexinfo->numIndexes; j++)
			{
				indexinfo->iscores[j].orgArrayIdx = j;
				indexinfo->iscores[j].indexinfo = indexinfo;
				if(!indexinfo->fields ||
				   !indexinfo->tbspec ||
				   !indexinfo->tbspec->flist)
				   indexinfo->iscores[j].score = 1;
				else
				   indexinfo->iscores[j].score =
					indexscore(indexinfo->fields[j],
						indexinfo->itypes[j],
						indexinfo->paths[j],
						indexinfo->sysindexParamsVals[j],
						dbtbl,
						indexinfo->tbspec->flist, fop,
						   param, paramIsRHS);

			}
			/* Need to sort */
			qsort(indexinfo->iscores, indexinfo->numIndexes,
				sizeof(INDEXSCORE), iscorecmp);

			/* Print scores *after* sorting, for consistent
			 * test output across platforms regardless of
			 * SYSINDEX ordering:
			 */
			if (TXtraceIndexBits & 0x1000)
			{
				int	i;
				SLIST	*df = indexinfo->tbspec->flist;
				char	*d, *e, dfBuf[1024], opBuf[128];

				if (df)		/* merge into CSV for msg */
				{
					d = dfBuf;
					e = dfBuf + sizeof(dfBuf);
					for (i = 0; i < df->cnt - 1; i++)
						if (d < e)
							d += htsnpf(d, e - d,
								    "%s%s",
						       (d > dfBuf ? "," : ""),
								    df->s[i]);
					if (d >= e)  /* `dfBuf' too small */
					{
            char *TruncationPoint = dfBuf + sizeof(dfBuf) - 4;
            strcpy(TruncationPoint, "...");
          }
				}
				else
					strcpy(dfBuf, "(null)");
				for (j = 0; j < indexinfo->numIndexes; j++)
				{
					i = indexinfo->iscores[j].orgArrayIdx;
					putmsg(MINFO, CHARPN,
"Score %d for index: %s table: %s FOP: %s index-type: %c desired-fields: %s index-fields: %s params: [%s]",
					       indexinfo->iscores[j].score,
					       indexinfo->paths[i],
					       dbtbl->lname,
				    TXqnodeOpToStr(fop, opBuf, sizeof(opBuf)),
					       indexinfo->itypes[i], dfBuf,
					       indexinfo->fields[i],
					    indexinfo->sysindexParamsVals[i]);
				}
			}
		}
		indexinfo->initialized++;
		start = 0;
	}
	else
	{
		start = indexinfo->lastreturn + 1;
	}
	switch(fop)
	{
		case FOP_TWIXT:
		case FOP_IN:
		case FOP_IS_SUBSET:
		case FOP_INTERSECT:
		case FOP_INTERSECT_IS_EMPTY:
		case FOP_INTERSECT_IS_NOT_EMPTY:
		case FOP_EQ:
		case FOP_GT:
		case FOP_GTE:
		case FOP_LT:
		case FOP_LTE:
		case FOP_MAT:
			for(j=start; j < indexinfo->numIndexes; j++)
				if (indexinfo->itypes[indexinfo->iscores[j].orgArrayIdx] == INDEX_BTREE &&
				    indexinfo->iscores[j].score > 0)
				{
					indexinfo->lastreturn = j;
					return indexinfo->iscores[j].orgArrayIdx;
				}
			break;
		case FOP_MM:
		case FOP_MMIN:
		case FOP_NMM:
		case FOP_PROXIM:
		case FOP_RELEV:
			for(j=start; j < indexinfo->numIndexes; j++)
			{
				if (indexinfo->iscores[j].score <= 0) continue;
				thistype = indexinfo->itypes[indexinfo->iscores[j].orgArrayIdx];
				if (thistype == INDEX_3DB ||
                                    thistype == INDEX_MM ||
                                    thistype == INDEX_FULL)
				{
					indexinfo->lastreturn = j;
					return indexinfo->iscores[j].orgArrayIdx;
				}
			}
			break;
		default:
			break;
	}
	return -1;
}

/******************************************************************/

INDEXINFO *closeindexinfo ARGS((INDEXINFO *));

INDEXINFO *
closeindexinfo(indexinfo)
INDEXINFO *indexinfo;
{
	indexinfo->paths = TXfreeStrList(indexinfo->paths,
					 indexinfo->numIndexes);
	indexinfo->fields = TXfreeStrList(indexinfo->fields,
					  indexinfo->numIndexes);
	indexinfo->sysindexParamsVals = TXfreeStrList(indexinfo->sysindexParamsVals,
				 indexinfo->numIndexes);
	indexinfo->itypes = TXfree(indexinfo->itypes);
	indexinfo->iscores = TXfree(indexinfo->iscores);
	resetindexinfo(indexinfo);
	return NULL;
}

/******************************************************************/

int
resetindexinfo(indexinfo)
INDEXINFO *indexinfo;
{
	if(indexinfo)
	{
		indexinfo->iscores = NULL;
		indexinfo->itypes = NULL;
		indexinfo->paths = NULL;
		indexinfo->fields = NULL;
		indexinfo->sysindexParamsVals = CHARPPN;
		indexinfo->numIndexes = 0;
		indexinfo->initialized = 0;
	}
	return 0;
}

/******************************************************************/
