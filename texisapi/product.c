#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

static int verbose = 0;

int
TXproductsetup(query, q, fo)
QNODE	*query;
QUERY	*q;
FLDOP	*fo;
{
	static char Fn[] = "productsetup";

	if (query->state == QS_ACTIVE)
		return 0;
	if(verbose)
	{
		char *x;

		x = TXdisppred(q->pred, 0, 0, 240);
		putmsg(MINFO, Fn, "Setting outer predicates to %s", x);
		TXfree(x);
	}
	if (q->prod == (PROD *)NULL)
		q->prod = doproductsetup(q);
	q->pr2 = TXmakepredvalid(q->pred,q->in1,1,0,1);
	TXsettablepred(query, q->in1,q->pr2,q->order,fo,1, NULL, NULL);
	if (TXdotree(query->left, fo, SQL_FETCH_NEXT, 1) == -1)
		return -1;
#ifndef NEVER
	TXsetprednames(q->pred,q->in2,1,0,1);
#endif
#ifdef HAVE_SUBSTPRED2
	if(!q->pred1)
	{
		q->pred1 = duppred(q->pred);
	}
	q->pr1 = substpred2(q->pred, q->pred1, q->in1);
#else
	q->pr1 = substpred(q->pred, q->in1);
#endif
#if defined(NEVER)
	TXsetpredalts(q->pred, q->out,1,0,1);
#endif
	if(verbose)
	{
		char *x;
		x = TXdisppred(q->pr1, 0, 0, 240);
		putmsg(MINFO, Fn, "Setting inner predicate to %s", x);
		free(x);
	}
#ifndef ALLOW_MISSING_FIELDS
#if !defined(HAVE_DUPPRED2)
	{
		PRED *pr3;

		q->in2->pred = TXclosepredvalid2(q->in2->pred);
		pr3=TXduppredvalid2(q->pr1,q->in2,1,0,1);
		TXsettablepred(query, q->in2,pr3,q->order,fo,0, NULL, NULL);
	}
#else
	TXsettablepred(query, q->in2, TXmakepredvalid(q->pr1, q->in2, 1, 0, 1), q->order, fo,1, NULL);
#endif
#else
	TXsettablepred(query, q->in2, q->pr1, q->order, fo,1, NULL);
#endif
	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;

	return 0;
}

int
TXproduct(query, q, fo)
QNODE *query;
QUERY *q;
FLDOP *fo;
{
	static char Fn[] = "product";
	int r;

	do {
		if(TXdotree(query->right, fo, SQL_FETCH_NEXT, 1) == -1)
		{
pnext_record:
			if(texispeekerr(q->out->ddic) ==
			   MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
				return -2;
			nullmms(q->pr1);
#if !defined(HAVE_DUPPRED2)
			q->in2->pred = TXclosepredvalid2(q->in2->pred);
#endif
#ifndef HAVE_SUBSTPRED2
			q->pr1 = closepred(q->pr1);
#endif
			TXsettablepred(query, q->in2, (PRED *)NULL, (PROJ *)NULL, fo,1, NULL, NULL);
#ifndef NO_DEL_TMP_JOIN
			TXdeltmprow(q->in1);
#endif
			if (TXdotree(query->left, fo, SQL_FETCH_NEXT, 1) == -1)
				return -1;
			if(verbose)
				putmsg(MINFO, NULL, "Rewind right");
			TXrewinddbtbl(query->right->q->out);
#ifdef HAVE_SUBSTPRED2
			q->pr1 = substpred2(q->pred, q->pred1, q->in1);
#else
			q->pr1=substpred(q->pred, q->in1);
#endif
			if(verbose)
			{
				char *x;

				x = TXdisppred(q->pr1, 0, 0, 240);
				putmsg(MINFO, Fn, "Setting inner predicate to %s", x);
				free(x);
			}
#if !defined(HAVE_DUPPRED2)
			{
			PRED *pr3;

			q->in2->pred = TXclosepredvalid2(q->in2->pred);
			pr3=TXduppredvalid2(q->pr1,q->in2,1,0,1);
			TXsettablepred(query, q->in2,pr3,q->order,fo,0, NULL, NULL);
			}
#else
			TXsettablepred(query, q->in2, TXmakepredvalid(q->pr1, q->in2, 1, 0, 1), q->order, fo, 1, NULL);
#endif
			if (TXdotree(query->right, fo, SQL_FETCH_NEXT, 1) == -1)
			{
#if !defined(HAVE_DUPPRED2)
			q->in2->pred = TXclosepredvalid2(q->in2->pred);
#endif
				TXsettablepred(query, q->in2, (PRED *)NULL, (PROJ *)NULL, fo, 1, NULL, NULL);
				goto pnext_record;
			}
		}
		r   = doproduct(q, q->pr1, fo);
	} while (r == -1);
	return r;
}
