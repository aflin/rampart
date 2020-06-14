/*
 * $Log$
 * Revision 1.17  2001/12/28 22:12:40  john
 * Use config.h
 *
 * Revision 1.16  2001-07-09 17:04:46-04  john
 * Function Name
 * 
 * Revision 1.15  2001-07-08 16:50:46-04  john
 * Compiler warnings.
 * 
 * Revision 1.14  2001-02-21 17:16:05-05  john
 * TX_DEBUG debugbreak.
 * 
 * Revision 1.13  1999-12-16 13:36:54-05  john
 * Allow for product only selecting some fields.
 * 
 * Revision 1.12  95/01/11  15:41:36  john
 * dostats extra arg.
 * 
 * Revision 1.11  94/09/23  11:16:11  john
 * Do some error checking.
 * 
 * Revision 1.10  94/08/09  12:59:16  john
 * Use new RECID scheme.
 * 
 * Revision 1.9  94/07/29  12:03:20  john
 * Add FLDOP parameters.
 * 
 * Revision 1.8  94/06/15  11:03:04  john
 * Remove unused vars.
 * 
 * Revision 1.7  94/04/05  15:30:19  john
 * Fix silly bug checking for NULL.
 * 
 * Revision 1.6  94/03/18  16:16:22  john
 * Check return codes.  Add putmsg.
 * 
 * Revision 1.5  94/03/09  12:24:59  john
 * Windows code
 * 
 * Revision 1.4  93/11/24  13:59:01  john
 * Return a result code.
 * 
 * Revision 1.3  93/11/22  12:23:50  john
 * Use fldcopy function.
 * 
 * Revision 1.2  93/11/05  12:15:41  john
 * Use PROD structure to make doproduct more re-entrant so it can be used
 * better in the interactive tree.
 * 
 * Revision 1.1  93/10/28  12:01:54  john
 * Initial revision
 * 
 * Revision 1.11  93/10/25  14:08:40  john
 * 
 * Comment.
 * 
 * Revision 1.10  93/10/21  16:05:07  john
 * Get headers right
 * 
 * Revision 1.9  93/10/20  12:48:50  john
 * Use db function.
 * 
 * Revision 1.8  93/10/11  16:31:12  john
 * -> K&R
 * 
 * Revision 1.7  93/10/07  13:52:15  john
 * Rname and tblid.
 * 
 * Revision 1.6  93/09/10  14:40:40  john
 * Remove tname.
 * Close the temp pred.
 * 
 * Revision 1.5  93/08/31  10:09:19  john
 * Add ddic to result table.
 * 
 * Revision 1.4  93/08/27  17:38:27  john
 * Changed the behaviour of tup_product so it won't return a differnt
 * table for output.
 * 
 * Revision 1.3  93/08/17  14:07:03  john
 * Add recid into the mix.
 * Use indexes if they exist.
 * 
 * Revision 1.2  93/08/09  16:45:38  john
 * Replace rewindtbl with rewinddbtbl.
 * Set type of temp table.
 * Use tupread instead of gettblrow.
 * 
 * Revision 1.1  93/08/04  16:44:48  john
 * Initial revision
 * 
*/

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

PROD *
closeprod(p)
PROD *p;
{
	if (p == (PROD *)NULL)
		return p;
	if (p->in1 != (FLD **)NULL)
		free(p->in1);
	if (p->in2 != (FLD **)NULL)
		free(p->in2);
	if (p->out != (FLD **)NULL)
		free(p->out);
	free(p);
	return (PROD *)NULL;
}

/******************************************************************/

PROD *doproductsetup2 ARGS((QUERY *));

PROD *
doproductsetup(q)
QUERY *q;
{
	static char Fn[] = "doproductsetup";
	DBTBL	*t1 = q->in1;
	DBTBL	*t2 = q->in2;
	DBTBL	*tout = q->out;
	FLD	**in1, **in2, **fout;
	int	i, n1, n2, nout, j;
	PROD	*prod;
	char	*name, fqname[80];

#ifndef NO_ANALYZE
	if(q->in1->ddic->optimizations[OPTIMIZE_ANALYZE])
		return doproductsetup2(q);
#endif
	prod = (PROD *)calloc(1, sizeof(PROD));
	if (prod == (PROD *)NULL)
		return prod;
/*	Allocate field pointers */

	n1 = ntblflds(t1->tbl);
	in1 = (FLD **)calloc(n1+1, sizeof(FLD *));
	n2 = ntblflds(t2->tbl);
	in2 = (FLD **)calloc(n2+1, sizeof(FLD *));
	nout = ntblflds(tout->tbl);
	fout = (FLD **)calloc(n1+n2+2, sizeof(FLD *));
	if (!in1 || !in2 || !fout)
	{
		putmsg(MERR+MAE, Fn, strerror(ENOMEM));
		return closeprod(prod);
	}

/*	Set up field pointers */

	j = 0;
	for (i = 0;
		(name = getfldname(t1->tbl, i)) != (char *)NULL;
		i++)
	{
		in1[i] = dbnametofld(t1, name);
		fout[j] = dbnametofld(tout, name);
		DBGMSG(0, (999, NULL, "Product field %d is %s", j, name));
		if (fout[j] == (FLD *) NULL)
		{
			strcpy(fqname, t1->lname);
			strcat(fqname, ".");
			strcat(fqname, name);
			fout[j] = dbnametofld(tout, fqname);
		}
		j++;
	}
	if (t1->lname && t1->lname[0])
	{
		n1++;
		in1[i]=dbnametofld(t1, "$recid");
		strcpy(fqname, t1->lname);
		strcat(fqname, ".$recid");
		fout[j++] = dbnametofld(tout, fqname);
	}
	for (i = 0;
		(name = getfldname(t2->tbl, i)) != (char *)NULL;
		i++)
	{
		in2[i] = dbnametofld(t2, name);
		fout[j] = dbnametofld(tout, name);
		DBGMSG(0, (999, NULL, "Product field %d is %s", j, name));
		if (fout[j] == (FLD *) NULL)
		{
			strcpy(fqname, t2->lname);
			strcat(fqname, ".");
			strcat(fqname, name);
			fout[j] = dbnametofld(tout, fqname);
		}
		j++;
	}
	if (t2->lname && t2->lname[0])
	{
		n2++;
		in2[i]=dbnametofld(t2, "$recid");
		strcpy(fqname, t2->lname);
		strcat(fqname, ".$recid");
		fout[j++] = dbnametofld(tout, fqname);
	}
	prod->n1 = n1;
	prod->n2 = n2;
	prod->nout = nout;
	prod->in1 = in1;
	prod->in2 = in2;
	prod->out = fout;
	return prod;
}

/****************************************************************************/
/*
 *	Perform a cartesion Product
 */

#define MAX(a,b) (a>b?a:b)

int
doproduct(q, pred, fo)
QUERY *q;
PRED *pred;
FLDOP *fo;
{
	DBTBL *t1 = q->in1;
	DBTBL *t2 = q->in2;
	DBTBL *tout = q->out;
	PROJ *proj = q->proj;
	PROD *prod = q->prod;
	DBTBL *tproj;
	FLD **in1, **in2, **fout;
	int  i, n1, n2, nout, rc;

/*	Open Tables */

	if (proj != (PROJ *)NULL)
	{
		tproj = tout;
		tout = TXtup_product_setup(t1, t2, 0, NULL);
		if (!tout)
			return -1;
		TXrewinddbtbl(tout);
	}
	else /* Compiler fodder so tproj looks initialized */
		tproj = NULL;

	in1 = prod->in1;
	in2 = prod->in2;
	fout = prod->out;
	n1 = prod->n1;
	n2 = prod->n2;
	nout = prod->nout;

/*	Get the product */

/*	WTF - Assign index to t1 based on pred */

	for (i=0; i < n1; i++)
		if (fout[i])
			fldcopy(in1[i], t1->tbl, fout[i], t2->tbl, fo);
/*	WTF - Assign index to t2 based on pred and values in t1 */

	for (i=0; i < n2; i++)
		if (fout[i+n1])
			fldcopy(in2[i], t1->tbl, fout[i+n1], t2->tbl, fo);
	if ((rc = tup_match(tout, pred, fo)) > 0)
	{
		if (proj)
			tup_project(tout, tproj, proj, fo);
		else
		{
			putdbtblrow(tout, NULL);
			dostats(tout, fo);
		}
	}


	tout->rank = MAX(t1->rank, t2->rank);


/*	Cleanup */

	if (proj != (PROJ *)NULL)
	{
		deltable(tout);
		return rc>0?0:-1;
	}
	else
		return rc>0?0:-1;
}

/****************************************************************************/
/******************************************************************/

/* Handle the case where we don't have all fields in the input tables */

/* Want to loop over the output table, and count fields from input
   tables */

PROD *
doproductsetup2(q)
QUERY *q;
{
	static char Fn[] = "doproductsetup";
	DBTBL	*t1 = q->in1;
	DBTBL	*t2 = q->in2;
	DBTBL	*tout = q->out;
	FLD	**in1, **in2, **fout;
	int	i, n1, n2, nout, j;
	PROD	*prod;
	char	*name;

	prod = (PROD *)calloc(1, sizeof(PROD));
	if (prod == (PROD *)NULL)
		return prod;

	n1=n2=nout=0;
	for(i = 0; (name = getfldname(tout->tbl, i)) != (char *)NULL; i++)
	{
		FLD *f1, *f2, *fo;

		f1 = dbnametofld(t1, name);
		f2 = dbnametofld(t2, name);
		fo = dbnametofld(tout, name);

#ifdef TX_DEBUG
		if(f1 && f2)
			debugbreak();
#endif
		
		if(f1)
			n1++;
		if(f2)
			n2++;
		if(fo)
			nout++;
	}

/*	Allocate field pointers */

	in1 = (FLD **)calloc(n1+1, sizeof(FLD *));
	in2 = (FLD **)calloc(n2+1, sizeof(FLD *));
	fout = (FLD **)calloc(n1+n2+2, sizeof(FLD *));

	if (!in1 || !in2 || !fout)
	{
		putmsg(MERR+MAE, Fn, strerror(ENOMEM));
		return closeprod(prod);
	}



/*	Set up field pointers */

	j = 0;
	n1=n2=nout=0;
	for(i = 0; (name = getfldname(tout->tbl, i)) != (char *)NULL; i++)
	{
		FLD *f1, *f2, *fo;

		f1 = dbnametofld(t1, name);
		f2 = dbnametofld(t2, name);
		fo = dbnametofld(tout, name);

#ifdef TX_DEBUG
		if(f1 && f2)
			debugbreak();
#endif
		
		if(f1)
		{
			in1[n1++] = f1;
		}
		if(f2)
		{
			in2[n2++] = f2;
		}
		if(fo)
			fout[nout++] = fo;
	}
	prod->n1 = n1;
	prod->n2 = n2;
	prod->nout = nout;
	prod->in1 = in1;
	prod->in2 = in2;
	prod->out = fout;
	return prod;
}
