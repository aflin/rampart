#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"


unsigned int fldno;
int valtbl ARGS((QNODE *, DBTBL *, FLDOP *));

/******************************************************************/

FLD *
TXqtreetofld(q, t, ffld, fo)
QNODE	*q;
DBTBL	*t;
int	*ffld;  /* (out) whether to free returned field */
FLDOP	*fo;
{
	FLD	*inf;

	if(ffld)
		*ffld = 0;
	switch(q->op)
	{
		case FIELD_OP:
			inf = (FLD *)q->tname;
			break;
		case PARAM_OP:
		{
			PARAM	*p;

			p = q->tname;
			if (p->fld)
				inf = p->fld;
			else
				inf = NULL;
			break;
		}
#ifdef NEVER
		case PROJECT_OP:
			inf = TXqueryfld(t->ddic,t,q,fo,SUBQUERY_SINGLEVAL,1);
			if(inf && ffld)
				*ffld = 1;
			break;
#endif
		default:
		{
			PRED	*pr;
			int	rc;

		/* This is where evaluation 
		   occurs to support func's etc */

			inf = NULL;
			pr = TXtreetopred(t->ddic, q, 0, fo, NULL);
			rc = pred_eval(t,pr,fo);
			closepred(pr);
			if(rc != -1)
				inf = fopop(fo);
			else
				inf = NULL;
#ifdef DEBUG
			DBGMSG(9, (999, NULL, "New Insert"));
#endif
			if(ffld)
				*ffld = 1;
		}
	}
	return inf;
}
/******************************************************************/
/*	Takes values as fields, and stuffs them into a table      */

int
valtbl(q, t, fo)
QNODE *q;
DBTBL *t;
FLDOP *fo;
{
	char		*fname;
	FLD		*fld;
	FLD		*inf, *outf;
	size_t		sz;
	int		freeifld = 0;

	switch (q->op)
	{
		case LIST_OP:            /* We have a list, so do the list */
			if (valtbl(q->left, t, fo) ||
			    valtbl(q->right, t, fo))
				return -1;
			break;
		default:
			fname = getfldname(t->tbl, fldno);
			if (!fname)
			{
				putmsg(MWARN, "Insert",
					"More Values Than Fields");
				return -1;
			}
			outf = dbnametofld(t, fname);

/* wtf Temporary hack to let fopush() be happy */
			if (getfld(outf, &sz) == (void *)NULL)
				setfldv(outf);
#ifdef NEVER
			switch(q->op)
			{
				case FIELD_OP:
					inf = (FLD *)q->tname;
					break;
				case PARAM_OP:
				{
					PARAM	*p;

					p = q->tname;
					if (p->fld)
						inf = p->fld;
					else
						inf = NULL;
					break;
				}
				default:
				{
					PRED	*pr;

				/* This is where evaluation 
				   occurs to support func's etc */

					inf = NULL;
					pr = TXtreetopred(t->ddic, q, 0, fo, NULL);
				        pred_eval(t,pr,fo);
					closepred(pr);
					inf = fopop(fo);
#ifdef DEBUG
				   	DBGMSG(9, (999, NULL, "New Insert"));
#endif
					freeifld = 1;
				}
			}
#else
			inf = TXqtreetofld(q, t, &freeifld, fo);
#endif
			if(!inf)
				return -1;
			fopush(fo, outf);
			fopush(fo, inf);
			if(foop(fo, FOP_ASN)!=0)
			{
				if((outf->type & DDTYPEBITS) == FTN_BLOB &&
					t->tbl->bf)
				{
					putmsg(MERR, "Assignment", "Handle Blob");
				}
				else
				{
				putmsg(MERR, "Assignment",
				 "Could not perform assignment to %s %s -> %s",
				 fname, ddfttypename(inf->type),
					ddfttypename(outf->type));
				fodisc(fo);/* Still need to remove from stack */
/* Needs to be NULLed, so we know it failed later */
				memset(outf->v, 0, outf->alloced);
				outf->v = NULL;
				}
			}
			else
			{
				fld = fopop(fo);
				setfld(outf, NULL, 0);
				closefld(outf->storage);
				memcpy(outf, fld, sizeof(FLD));
				free(fld);
			}
			fldno ++;
			if(freeifld && inf)
				closefld(inf);
			break;
	}
	return 0;
}

/******************************************************************/

int
valuestotbl(q, t, fo)
QNODE *q;
DBTBL *t;
FLDOP *fo;
{
	int i;

	fldno = 0;
	if(valtbl(q->left, t, fo)==-1)
		return -1;
	for(i=0; i < t->tbl->n; i++)
	{
		if(!getfld(t->tbl->field[i], NULL))
		{
			putmsg(MERR,"Insert","Insufficient values for fields");
			return -1;
		}
	}
	puttblrow(t->tbl, NULL);
	return 0;
}
