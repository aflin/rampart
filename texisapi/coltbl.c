#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

static int putcoltbl ARGS((QNODE *, DBTBL *, FLDOP *));

/******************************************************************/

static int
fldtodefault(FLD *f)
{
	FLD *f2;

	setfldv(f);
	if(fldisvar(f))
		f->n = 0;			/* ie. empty */
	memset(f->shadow, 0, f->alloced);
	switch(f->type & DDTYPEBITS)
	{
		case FTN_BLOBI:
			f2 = f->storage;
			if(f2)
			{
				ft_blob *v;

				setfldv(f2);
				v = getfld(f2, NULL);
				if(v)
				{
					*v = -1;
				}
			}
			break;
		case FTN_STRLST:
			/* KNG 030722  must at least have ft_strlst struct: */
			f->size = f->n = sizeof(ft_strlst);
			break;
	}
	return 0;
}

/******************************************************************/

static int ltotbl ARGS((QNODE *, DBTBL *, FLDOP *));

static int
ltotbl(q, t, fo)
QNODE *q;
DBTBL *t;
FLDOP *fo;
{
	switch (q->op)
	{
		case LIST_OP:
			if(ltotbl(q->left, t, fo)==-1)
				return -1;
			if(ltotbl(q->right, t, fo)==-1)
				return -1;
			break;
		case COLUMN_OP:
			if(putcoltbl(q, t, fo)==-1)
				return -1;
			break;
		default:
			return -1;
	}
	return 0;
}

/******************************************************************/

void
listtotbl(q, t, fo)
QNODE *q;
DBTBL *t;
FLDOP *fo;
{
	int	i;

/* WTF drill */
	for (i=0; i < ndbtblflds(t); i++)
		if(fldisvar(t->tbl->field[i]))
			t->tbl->field[i]->n = 0;
	if(ltotbl(q, t, fo) == -1)
		return;
	for (i=0; i < ndbtblflds(t); i++)
		if(!getfld(t->tbl->field[i], NULL))
		{
#ifndef NO_INSERT_DEFAULT
			fldtodefault(t->tbl->field[i]);
#else
			putmsg(MERR,"Insert","Insufficient values for fields");
			return ;
#endif
		}
	puttblrow(t->tbl, NULL);
}

/******************************************************************/
/* Put the field in q->right into the field named by q->left in
 * table t
 */

static int
putcoltbl(q, t, fo)
QNODE *q;
DBTBL *t;
FLDOP *fo;
{
	FLD	*fld, *u;
	int	freefld;

	if(q->right->op == PROJECT_OP)
	{
		putmsg(MERR, NULL, "INSERT INTO (field) SELECT fields FROM ... syntax not supported");
		return -1;
	}
	fld = dbnametofld(t, q->left->tname);
	if(!fld)
	{
		if(t->ddic->options[DDIC_OPTIONS_IGNORE_MISSING_FIELDS])
			return 0;
		if(q->left->tname)
		putmsg(MWARN, NULL, "%s is not a valid field", q->left->tname);
		else
		putmsg(MWARN, NULL, "(null) is not a valid field");
		return -1;
	}
#ifdef NEVER
	switch(q->right->op)
	{
		case PARAM_OP:
			p = (PARAM *)q->right->tname;
			u = p->fld;
			break;
		default:
			u = (FLD *)q->right->tname;
	}
#else
	u = TXqtreetofld(q->right, t, &freefld, fo);
#endif
	if(u && ((TXfldbasetype(u) == FTN_COUNTERI && TXfldbasetype(fld) == FTN_COUNTER) ||
	   getfld(u, NULL)))
		_fldcopy(u, NULL, fld, t->tbl, fo);
	else
		return -1;

	/* if(u->type == FTN_COUNTERI && fld->type == FTN_COUNTER) */
	/* { */
	/* 	if(!fld->v) */
	/* 		putfld(fld, getcounter(t->ddic), 1); */
	/* } */

	if(freefld)
		closefld(u);
	return 0;
}

/******************************************************************/
/*
 *	WTF  This is incomplete at present.  A list of columns and
 *	values need to be added, and the rest left NULL.
 */

void
columntotbl(q, t, fo)
QNODE *q;
DBTBL *t;
FLDOP *fo;
{
#ifndef NEVER
#ifdef NO_INSERT_DEFAULT
	static char Fn[] = "Insert";
#endif
	int	i;

	for (i=0; i < t->tbl->n; i++)
		if(fldisvar(t->tbl->field[i]))
			t->tbl->field[i]->n = 0;
	if(putcoltbl(q, t, fo) == -1)
		return;
	for (i=0; i < ndbtblflds(t); i++)
		if(!getfld(t->tbl->field[i], NULL))
		{
#ifndef NO_INSERT_DEFAULT
			fldtodefault(t->tbl->field[i]);
#else
			putmsg(MERR,Fn,"Insufficient values for fields");
			return ;
#endif
		}
	puttblrow(t->tbl, NULL);
#else /* NEVER */
	static char Fn[] = "columntotbl";

	putmsg(MWARN+UGE, Fn, "This syntax not yet supported");
#endif /* NEVER */
}
