/*
 * $Log$
 * Revision 1.13  2009/01/08 23:08:35  john
 * Fix prototype.
 *
 * Revision 1.12  2009/01/07 21:53:27  john
 * Don't walk into CREATE TABLE when counting params.
 *
 * Revision 1.11  2009/01/07 21:38:34  john
 * Fix up enumparams so it allocates the buffer for parameters before assigning pointers in the tree.  Could be an issue with more than 16 parameters and parameters both in select and where clauses.
 *
 * Revision 1.10  2001/12/28 22:14:46  john
 * Use config.h
 *
 * Revision 1.9  2001-07-09 17:04:22-04  john
 * Compiler warnings
 *
 * 
 * Revision 1.8  1999-12-16 15:27:26-05  john
 * Params becomes dynamically sized array associated with statement.
 *
 * Revision 1.6  95/12/19  11:34:20  john
 * Use fld calls.
 * 
 * Revision 1.5  95/01/11  12:51:35  john
 * Add header.
 * 
 * Revision 1.4  94/12/08  13:22:26  john
 * Free fields when enumeratine.
 * 
 * Revision 1.3  94/07/29  12:04:46  john
 * Add an unset param function.
 * 
 * Revision 1.2  94/06/15  11:05:22  john
 * Ifdef RCSID (and get it right).
 * Remove unused var.
 * 
 * Revision 1.1  94/04/13  17:06:34  john
 * Initial revision
 *
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

static int expandparams ARGS((LPSTMT, size_t));

static int
expandparams(lpstmt, desired)
LPSTMT	lpstmt;
size_t desired;
{
	PARAM *nparam;
	size_t nalloced;

	if(desired < lpstmt->allocedparam)
		return 0;
	nalloced = (((desired >> 4) + 1) << 4);
	nparam = (PARAM *)calloc(nalloced, sizeof(PARAM));
	if(nparam)
	{
		memcpy(nparam,lpstmt->param,lpstmt->allocedparam*sizeof(PARAM));
		if(lpstmt->param)
			free(lpstmt->param);
		lpstmt->param = nparam;
		lpstmt->allocedparam = nalloced;
		return 0;
	}
	return -1;
	
}

/******************************************************************/

static size_t countparams(LPSTMT lpstmt, QNODE *q, size_t np);

static size_t
countparams(LPSTMT lpstmt, QNODE *q, size_t np)
{
	size_t pn;

	if (!q)
		return np;
	switch(q->op)
	{
		case PARAM_OP:
			pn = (size_t)q->left;
			if(pn > np)
				return pn;
			break;
		default:
			np = countparams(lpstmt, q->right, np);
		case TABLE_AS_OP:
			np = countparams(lpstmt, q->left, np);
		case TABLE_OP: break;
	}
	return np;
}

/******************************************************************/

int
TXenumparams(LPSTMT lpstmt, QNODE *q, int intree, size_t *paramcount)
{
	PARAM	*p;
	size_t pn, pc;
	int rc = 0;

	if (!q)
		return rc;
	if(intree == 0)
	{
		pc = countparams(lpstmt, q, 0);
		if(paramcount)
			*paramcount = pc;
		if(expandparams(lpstmt, pc) == -1)
		{
			return -1;
		}
	}
	switch(q->op)
	{
		case PARAM_OP:
			pn = (size_t)q->left;
			if(expandparams(lpstmt, pn) == 0)
			{
				p = q->tname = &lpstmt->param[pn];
				p->needdata = 1;
				break;
			}
			else
				return -1;
		default:
			rc = TXenumparams(lpstmt, q->right, 1, NULL);
			if(rc == -1)
				break;
		case TABLE_AS_OP:
			rc = TXenumparams(lpstmt, q->left, 1, NULL);
		case TABLE_OP: break;
	}
	return rc;
}

/******************************************************************/

PARAM *
getparam(lpstmt, q, np)
LPSTMT	lpstmt;
QNODE	*q;
int	np;
{
	if (!q)
		return (PARAM *)NULL;
	if(expandparams(lpstmt, np) == 0)
	{
#ifdef NEVER
	putmsg(999, NULL, "Got parameter %d to %lx", np, &lpstmt->param[np]);
#endif
		return &lpstmt->param[np];
	}
	return NULL;
}

/******************************************************************/

PARAM *
TXneeddata(q, discparam)
QNODE *q;
int discparam; /* Param-o-rama - Ignore where clause */
{
	PARAM	*p;

	if (!q)
		return (PARAM *)NULL;
	switch(q->op)
	{
		case PARAM_OP:
			p = q->tname;
			if (p->needdata)
				return p;
			else
				return (PARAM *)NULL;
		case TABLE_OP:
			return (PARAM *)NULL;
		case SELECT_OP:
			p = TXneeddata(q->left, discparam);
			if (p)	return p;
			if(discparam)
				return NULL;
			else
				return TXneeddata(q->right, discparam);
		case TABLE_AS_OP:
			return TXneeddata(q->left, discparam);
		default:
			p = TXneeddata(q->left, discparam);
			if (p)	return p;
			return TXneeddata(q->right, discparam);
	}
}

/******************************************************************/

int
TXparamunset(q, discparam)
QNODE *q;
int discparam; /* Param-o-rama - Ignore where clause */
{
	PARAM	*p;
	int	rc;

	if (!q)
		return 0;
	switch(q->op)
	{
		case PARAM_OP:
			p = q->tname;
			if (p->needdata)
				return 0;
			if (p->fld)
				return 0;
			return 1;
		case TABLE_OP:
			return 0;
		case SELECT_OP:
			rc = TXparamunset(q->left, discparam);
			if (rc)	return rc;
			if(discparam)
				return 0;
			else
				return TXparamunset(q->right, discparam);
			
		case TABLE_AS_OP:
			return TXparamunset(q->left, discparam);
		default:
			rc = TXparamunset(q->left, discparam);
			if (rc)	return rc;
			return TXparamunset(q->right, discparam);
	}
}

/******************************************************************/

int
TXresetparams(lpstmt)
LPSTMT lpstmt;
{
	size_t i;

	for(i=0; i < lpstmt->allocedparam; i++)
		lpstmt->param[i].needdata = 1;
	return 0;
}

/******************************************************************/

static int paramcount=0;


void
TXresetparamcount()
{
	paramcount=0;
}

/******************************************************************/

int
TXnextparamnum()
{
	return ++paramcount;
}

/******************************************************************/

