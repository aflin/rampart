#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

/******************************************************************/
/*
	Find the NFLDSTAT structure that matches the predicate.
*/

static NFLDSTAT *findstat ARGS((NFLDSTAT *, PRED *));

static NFLDSTAT *
findstat(rc, pred)
NFLDSTAT *rc;	/* Table to search */
PRED *pred;	/* The predicate we are looking for */
{
	while(rc)
	{
		if(TXpredcmp(rc->pred, pred))
			return rc;
		rc = rc->next;
	}
	return NULL;
}

/******************************************************************/
/*
	Create a field of the appropriate type to hold the
	aggregate.  This should probably just be a generic
	genpredfld, to generate a field for whatever type
	of result.  Currently we have no need for it though.
*/

static FLD *genstatfld ARGS((DBTBL *, PRED *, FLDOP *));

static FLD *
genstatfld(table, pred, fo)
DBTBL *table;
PRED *pred;
FLDOP *fo;
{
	char *newfldtype;
	int size, nonnull;
	FLD *fld = NULL;

	newfldtype = predtype(pred, table, fo, &size, &nonnull);
	if(newfldtype)
		fld = createfld(newfldtype, size, nonnull);
	return fld;
}

/******************************************************************/
/*
	Add the needed NFLDSTAT members to the table to be able
	to evalute the given predicate.
*/

static int addstatpred ARGS((DBTBL *, PRED *, FLDOP *, NFLDSTAT **));

static int
addstatpred(table, pred, fo, fs)
DBTBL *table;	/* Table we are working on */
PRED *pred;	/* Predicate we are trying to evaluate */
FLDOP *fo;	/* Fldmath stack */
NFLDSTAT **fs;	/* The fldstat we are playing with */
{
	NFLDSTAT *nfs;

	if(!pred)
		return 0;

	if(pred->op == AGG_FUN_OP)
	{
		DBGMSG(9,(999,NULL,"Need to handle %s", disppred(pred, 0, 0)));
		if(!strcmp(pred->left, "avg"))
		{
			PRED *p2;

			p2 = duppred(pred);
			free(p2->left);
			p2->left = strdup("count");
			addstatpred(table, p2, fo, fs);
			free(p2->left);
			p2->left = strdup("sum");
			addstatpred(table, p2, fo, fs);
			closepred(p2);
			return 0;
		}
		if((nfs=findstat(*fs, pred)) != NULL)
		{
			DBGMSG(9,(999, NULL, "Already there"));
			nfs->inuse = 1;
			return 0;
		}
		else
		{
			nfs = (NFLDSTAT *)calloc(1, sizeof(NFLDSTAT));
			nfs->pred = duppred(pred);
			nfs->next = *fs;
			nfs->fld = genstatfld(table, pred, fo);
			nfs->inuse = 1;
			if((table->nfldstat && table->nfldstat->needfield) ||
			   strcmp(pred->left, "count"))
			{
				nfs->needfield = 1;
			}
			*fs = nfs;
			return 0;
		}
	}
	if(pred->lt == 'P')
		addstatpred(table, pred->left, fo, fs);
	if(pred->rt == 'P')
		addstatpred(table, pred->right, fo, fs);
	return 0;
}

/******************************************************************/

int
TXisprojcountonly(DBTBL *dbtbl, PROJ *proj, FLDOP *fo)
{
	int i;
	PRED *p;
	NFLDSTAT *nf;

	(void)fo;
	for(nf = dbtbl->nfldstat; nf; nf = nf->next)
	{
		if(nf->inuse == 0)
			continue;
		if(strcmp(nf->pred->left, "count") ||
		   TXpredrtdist(nf->pred))
		{
			dbtbl->nfldstatcountonly = 0;
			return 0;
		}
	}
	dbtbl->nfldstatcountonly = 1;
	for(i=0; i < proj->n; i++)
	{
		p = proj->preds[i];
		if(p->op == RENAME_OP && p->lt == 'P')
			p = p->left;
		if(p->op != AGG_FUN_OP)
			return 0;
	}
	return 1;
}

/******************************************************************/

int
TXsetcountstat(nf,count)
NFLDSTAT *nf;
EPI_HUGEUINT	count;
{
	int nc = 0;
	for(; nf; nf = nf->next)
	{
		if(nf->inuse == 0)
			continue;
		if(strcmp(nf->pred->left, "count"))
			return 0;
		else
		{
			ft_long	*lp;

			if((lp = getfld(nf->fld, NULL)))
			{
				*lp = (ft_long)count;
			}
			else
			{
				setfldv(nf->fld);
				lp = getfld(nf->fld, NULL);
				*lp = (ft_long)count;
			}
			nc++;
		}
	}
	return nc;
}

/******************************************************************/

int
TXopennewstats(table, proj, fo, fs)
DBTBL *table;
PROJ *proj;
FLDOP *fo;
NFLDSTAT **fs;
{
	int i;
	NFLDSTAT *nf;

	for(nf = *fs; nf; nf = nf->next)
	{
		nf->inuse = 0;
	}
	for(i=0; i < proj->n; i++)
		addstatpred(table, proj->preds[i], fo, fs);
	return 0;
}

/******************************************************************/

NFLDSTAT *
TXdupnewstats(fs)
NFLDSTAT *fs;
{
	static CONST char	fn[] = "TXdupnewstats";
	NFLDSTAT *rc;

	if(!fs)
		return fs;
	rc = (NFLDSTAT *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(NFLDSTAT));
	rc->pred = fs->pred;
	rc->fld = dupfld(fs->fld);
	rc->needfield = fs->needfield;
	rc->inuse = fs->inuse;
	rc->isdup = 1;
	rc->next = TXdupnewstats(fs->next);
	rc->unique_values.btree = NULL;
	rc->unique_values.table = NULL;
	return rc;
}

/******************************************************************/

#ifdef FLDMAT_COUNT
static ft_long one = 1;
#endif

int
TXaddstatrow(nf, table, fo)
NFLDSTAT *nf;
DBTBL *table;
FLDOP *fo;
{
#ifdef FLDMATH_COUNT
	static FLD *fone = NULL;
#endif
	FLD *fld = NULL, *f1;
	int ffld=0;
	PRED *p;

	for(; nf; nf = nf->next)
	{
		if(!nf->inuse)
			continue;
		if(ffld)
		{
			closefld(fld);
			ffld=0;
		}
		p = nf->pred;

		if(TXpredrtdist(p) || strcmp(p->left, "count"))
		{
		if(p->rt == FIELD_OP)
		{
			fopush(fo, p->right);
			fld=p->right;
		}
		if(p->rt == NAME_OP)
		{
			if (p->rat == FIELD_OP)
			{
				fld = p->altright;
				fopush(fo, p->altright);
			}
			else
			{
				fld = dbnametofld(table, p->right);
				if(fld)
				{
					p->rat = FIELD_OP;
					p->altright = fld;
					fopush(fo, fld);
				}
				else if(strcmp(p->right, "$star"))
				/* Don't really care about '*' */
				{
					DBGMSG(9,(999,NULL,"Field %s not found",
								  p->right));
					continue;
				}
			}
		}
		if(p->rt == 'P')
		{
			pred_eval(table, p->right, fo);
			fld=dupfld(fopeek(fo));
			ffld=1;
		}
		}

		if(TXpredrtdist(p))
		{
			void *v;
			size_t sz;
			BTLOC btloc;

			if(nf->unique_values.btree == NULL)
			{
				DD *dd;

				dd = opennewdd(1);
				putdd(dd, "CDF", (char *)TXfldtypestr(fld),
				      fld->n, 0);
				nf->unique_values.table = createtbl(dd, NULL);
				nf->unique_values.btree = openbtree(NULL, BT_MAXPGSZ, 20, BT_UNIQUE, O_RDWR | O_CREAT);
				btreesetdd(nf->unique_values.btree, dd);
				nf->unique_values.btree->usr =
					TXopenfldcmp(nf->unique_values.btree,
						 TXOPENFLDCMP_INTERNAL_FLDOP);
				dd = closedd(dd);
			}
			v = getfld(fld, &sz);
			putfld(nf->unique_values.table->field[0], v, sz);
			sz = fldtobuf(nf->unique_values.table);
			/* Bug 6451: `select count(distinct x)' could
			 * yield bad (too high) count if `btloc' is
			 * RECID_DELETED (was uninitialized):
			 */
			TXsetrecid(&btloc, RECID_INVALID);
			if(btinsert(nf->unique_values.btree, &btloc, sz, nf->unique_values.table->orec) == -1)
				continue;
		}

		if(!strcmp(p->left, "count"))
		{
#ifndef FLDMATH_COUNT
			ft_long	*lp;

			if((lp = getfld(nf->fld, NULL)))
			{
				*lp = *lp + 1;
			}
			else
			{
				setfldv(nf->fld);
				lp = getfld(nf->fld, NULL);
				*lp = 1;
			}
#else
			if(!fone)
			{
				fone = createfld("long", 1, 1);
				putfld(fone, &one, 1);
			}
			fopush(fo, fone);
			if (getfld(nf->fld, NULL))
			{
				fopush(fo, nf->fld);
				foop(fo, FOP_ADD);
			}
			closefld(nf->fld);
			nf->fld = fopop(fo);
#endif
			/*
			The value or field that we are counting was on the
			stack when we started, and is not needed.  If we
			ever support NULL to count non-NULL then we'll need to
			look at the value to see if it should be counted.
			*/
			/*

			fodisc(fo);

			Apparently this doesn't work and breaks some tests,
			e.g. test145
			*/
			continue;
		}

		if(!strcmp(p->left, "sum"))
		{
			if(getfld(nf->fld, NULL))
			{
				fopush(fo, nf->fld);
				foop(fo, FOP_ADD);
			}
			closefld(nf->fld);
			nf->fld = fopop(fo);
			continue;
		}

		if(!strcmp(p->left, "min"))
		{
			if(getfld(nf->fld, NULL))
			{
				fopush(fo, nf->fld);
				foop(fo, FOP_LT);
				f1 = fopeek(fo);
				if (f1 && *(ft_int *)getfld(f1, NULL))
				{
					closefld(nf->fld);
					if(ffld)
					{
						nf->fld = fld;
						ffld=0;
					}
					else
					{
						nf->fld = dupfld(fld);
					}
				}
				fodisc(fo);
			}
			else
			{
				fodisc(fo);
				closefld(nf->fld);
				if(ffld)
				{
					nf->fld = fld;
					ffld=0;
				}
				else
				{
					nf->fld = dupfld(fld);
				}
			}
			continue;
		}

		if(!strcmp(p->left, "max"))
		{
			if(getfld(nf->fld, NULL))
			{
				fopush(fo, nf->fld);
				foop(fo, FOP_GT);
				f1 = fopeek(fo);
				if (f1 && *(ft_int *)getfld(f1, NULL))
				{
					closefld(nf->fld);
					if(ffld)
					{
						nf->fld = fld;
						ffld=0;
					}
					else
					{
						nf->fld = dupfld(fld);
					}
				}
				fodisc(fo);
			}
			else
			{
				fodisc(fo);
				closefld(nf->fld);
				if(ffld)
				{
					nf->fld = fld;
					ffld=0;
				}
				else
				{
					nf->fld = dupfld(fld);
				}
			}
			continue;
		}

	}
	if(ffld)
	{
		closefld(fld);
		ffld=0;
	}
	return 0;
}

/******************************************************************/

FLD *
TXgetstatfld(t, p)
DBTBL *t;
PRED *p;
{
	NFLDSTAT *nf;

	nf = findstat(t->nfldstat, p);
	if(!nf)
		return NULL;
	return nf->fld;
}
/******************************************************************/

int
TXresetnewstats(nf)
NFLDSTAT *nf;
{
	for(; nf; nf = nf->next)
	{
		nf->fld->v = NULL;
		if(nf->unique_values.btree)
		{
			nf->unique_values.btree->usr  =
				TXclosefldcmp(nf->unique_values.btree->usr);
			nf->unique_values.btree = closebtree(nf->unique_values.btree);
		}
		nf->unique_values.table = closetbl(nf->unique_values.table);
#ifdef NEVER
		nf->inuse = 0;
#endif
	}
	return 0;
}

/******************************************************************/

int
TXclosenewstats(nfp)
NFLDSTAT **nfp;
{
	NFLDSTAT *nf, *nnf;

	for(nf = *nfp; nf; nf = nnf)
	{
		closefld(nf->fld);
		if(nf->unique_values.btree)
		{
			nf->unique_values.btree->usr =
				TXclosefldcmp(nf->unique_values.btree->usr);
			nf->unique_values.btree =
				closebtree(nf->unique_values.btree);
		}
		closetbl(nf->unique_values.table);
		/* Duped stat points to same pred */
		if(!nf->isdup)
			closepred(nf->pred);
		nnf = nf->next;
		free(nf);
	}
	*nfp = NULL;
	return 0;
}

/******************************************************************/

int
TXcopystats(nfp, ofp)
NFLDSTAT *nfp, *ofp;
{
	FLD *tf;
	NFLDSTAT *nf, *of;

	for(nf = nfp; nf; nf=nf->next)
	{
		for(of = ofp; of; of = of->next)
		{
			if(!TXpredcmp(nf->pred, of->pred))
				continue;
			tf = of->fld;
			of->fld = nf->fld;
			nf->fld = tf;
			break;
		}
	}
	return 0;
}
