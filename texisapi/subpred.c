/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

static void subpred ARGS((PRED *, DBTBL *));

static void
subpred(np, db)
PRED *np;
DBTBL *db;
{
	FLD *f;
        FOP fldOp;

	if (np == (PRED *)NULL)
		return;
	if (np->lt == 'P')
		subpred(np->left, db);
	if (np->rt == 'P')
		subpred(np->right, db);
	if (np->lt == NAME_OP && np->rt == NAME_OP)
	{
		f = dbnametofld(db, (char *)np->right);
		if (f)
		{
			np->rt = FIELD_OP;
			free(np->right);
			np->right = (void *)dupfld(f);
		}
		else
		{
			f = dbnametofld(db, (char *)np->left);
			if (f)
			{
				np->lt = FIELD_OP;
				free(np->left);
				np->left = (void *)dupfld(f);
			}
		}
	}	
	if (np->lt == NAME_OP && np->rt == FIELD_OP && TXismmop(np->op, &fldOp))
	{
		FLD *f1 = (FLD *)np->right;
		void	*v;

		v = getfld(f1, NULL);
		if(v)
			setddmmapi(db, v, fldOp);
	}
}

/******************************************************************/

PRED *
substpred2(op, np, db)
PRED *op, *np;
DBTBL *db;
{
	FLD *f;
        FOP fldOp;

	if (op == (PRED *)NULL)
		return op;
	if (op->lt == 'P')
		substpred2(op->left, np->left, db);
	if (op->rt == 'P')
		substpred2(op->right, np->right, db);
	if (op->lt == NAME_OP && op->rt == NAME_OP)
	{
		if(op->rat == FIELD_OP)
		{
			f = op->altright;
		}
		else
		{
			f = dbnametofld(db, (char *)op->right);
			if(f)
			{
				op->rat = FIELD_OP;
				op->altright = f;
			}
		}
		if (f)
		{
			if(np->rt == FIELD_OP)
				closefld(np->right);
			else
				free(np->right);
			np->rt = FIELD_OP;
			np->right = (void *)dupfld(f);
		}
		else
		{
			if(op->lat == FIELD_OP)
			{
				f = op->altleft;
			}
			else
			{
				f = dbnametofld(db, (char *)op->left);
				if(f)
				{
					op->lat = FIELD_OP;
					op->altleft = f;
				}
			}
			if (f)
			{
				if(np->lt == FIELD_OP)
					closefld(np->left);
				else
					free(np->left);
				np->lt = FIELD_OP;
				np->left = (void *)dupfld(f);
			}
		}
	}	
	if (np->lt == NAME_OP && np->rt == FIELD_OP && TXismmop(np->op, &fldOp))
	{
		FLD *f1 = (FLD *)np->right;
		void	*v;

		v = getfld(f1, NULL);
		if(v)
			setddmmapi(db, v, fldOp);
	}
	return np;
}

/******************************************************************/
/*	Substitute variables in p that are present in db by their
 *	current values.
 */

PRED *
substpred(p, db)
PRED *p;
DBTBL *db;
{
	PRED *np;
	FLD *f;
        FOP fldOp;

	if (p == (PRED *)NULL)
		return (PRED *)NULL;
	np = duppred(p);
	if (np->lt == 'P')
		subpred(np->left, db);
	if (np->rt == 'P')
		subpred(np->right, db);
	if (np->lt == NAME_OP && np->rt == NAME_OP)
	{
		f = dbnametofld(db, (char *)np->right);
		if (f)
		{
			np->rt = FIELD_OP;
			free(np->right);
			np->right = (void *)dupfld(f);
			return np;
		}
		f = dbnametofld(db, (char *)np->left);
		if (f)
		{
			np->lt = FIELD_OP;
			free(np->left);
			np->left = (void *)dupfld(f);
			return np;
		}
	}	
	if (np->lt == NAME_OP && np->rt == FIELD_OP && TXismmop(np->op, &fldOp))
	{
		FLD *f1 = (FLD *)np->right;
		void	*v;

		v = getfld(f1, NULL);
		if(v)
			setddmmapi(db, v, fldOp);
	}
#ifdef DEBUG
	DBGMSG(5,(999, NULL, "%s ---> %s", disppred(p,0, 0), disppred(np,0, 0)));
#endif
	return np;
}
