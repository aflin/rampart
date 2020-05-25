/*
 * $Log$
 * Revision 1.12  2002/10/30 15:22:35  mark
 * headers
 *
 * Revision 1.11  2001-12-28 17:14:32-05  john
 * Use config.h
 *
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/
static void dispddmm ARGS((MMLST *, PRED *));

static void
dispddmm(mhl, p)
MMLST	*mhl;
PRED *p;
{
        FLD     *f = (FLD *)p->right;
	DDMMAPI *d = (DDMMAPI *)getfld(f, NULL);
        char    *n = (char *)NULL;

	if (p->lt == 'P')           /* MAW 06-27-94 - get fldname too */
                n = TXdisppred(p->left, 1, 0, 80);
	else if (p->lt == NAME_OP)
		n = p->left;
	if(p->value > 0)  /* JMT 01-12-95 - only if mm hit */
		addmmlst(mhl, (void *)d->mmapi, d->buffer, n);
}

/******************************************************************/
static void dpredicate ARGS((MMLST *, PRED *));

static void
dpredicate(mhl, p)
MMLST	*mhl;
PRED *p;
{
	if (p->lt == 'P')
		dpredicate(mhl, p->left);
	if (p->rt == FIELD_OP)
#ifdef NEVER
		if (p->op == FOP_MM || p->op == FOP_NMM)
#else
		if (p->op == FOP_MM)
#endif
			dispddmm(mhl,p);
	if (p->rt == 'P')
		dpredicate(mhl, p->right);
}

/******************************************************************/
static void dproject ARGS((MMLST *, PROJ *));

static void
dproject(mhl, p)
MMLST	*mhl;
PROJ *p;
{
	int i;

	for (i=0; i< p->n; i++)
		dpredicate(mhl, p->preds[i]);
}

/******************************************************************/
static void dreason ARGS((MMLST *, QNODE *));

static void
dreason(mhl, query)
MMLST	*mhl;
QNODE	*query;
{
	switch(query->op) {
		case NAME_OP:
			break;
		case RENAME_OP:
			dreason(mhl, query->left);
			break;
		case PRODUCT_OP:
			dreason(mhl, query->left);
			dreason(mhl, query->right);
			break;
		case PROJECT_OP:
			dreason(mhl, query->left);
			dproject(mhl, query->q->proj);
			break;
		case SELECT_OP:
			dreason(mhl, query->left);
			dpredicate(mhl, query->q->pred);
			break;
    default:
    /* Shouldn't be any PREDs under other operations */
      break;
	}
}

/******************************************************************/
/*
	Get a list of metamorph structures.

	This takes a handle to a statement, and returns a list of
	metamorph handles which occur within the statement.
*/

MMLST *
getmmlst(hstmt)
HSTMT hstmt;
{
	MMLST	*mhl;
	QNODE	*query;

	query = ((LPSTMT)hstmt)->query;
	mhl = openmmlst();
	if (mhl)
		dreason(mhl, query);
	return mhl;
}
